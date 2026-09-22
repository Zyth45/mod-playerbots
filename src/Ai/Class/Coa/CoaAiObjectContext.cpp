/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaAiObjectContext.h"

#include "Action.h"
#include "AttackAction.h"
#include "CoaSpecialization.h"
#include "CombatStrategy.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "MovementActions.h"
#include "NamedObjectContext.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Strategy.h"
#include "Trigger.h"
#include "mod-ascension-compat/src/AscensionSpecialization.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <ctime>
#include <map>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// CoaGroupTelemetry.cpp: counts a taunt, or a heal tried, in the group fight being measured.
void CoaTelemetryNoteTaunt(Player* bot);
void CoaTelemetryNoteHeal(Player* bot, uint32 spellId, uint16 outcome);

namespace
{

// What an ability does, read from its effects. An ability can do several of these.
enum AbilityKind : uint16
{
    KIND_HEAL       = 0x0001,  // heals an ally
    KIND_TAUNT      = 0x0002,
    KIND_AOE        = 0x0004,  // hits enemies in an area
    KIND_BUFF       = 0x0008,  // long positive aura on the caster or allies
    KIND_DEFENSIVE  = 0x0010,  // short damage reduction, absorb or avoidance on the caster
    KIND_HOSTILE    = 0x0020,  // aimed at an enemy
    KIND_DAMAGE     = 0x0040,
    KIND_GROUP_HEAL = 0x0080,  // heals allies in an area
    KIND_HOT        = 0x0100,  // heal over time
    KIND_ALLY_CAST  = 0x0200,  // can be cast on another group member
    KIND_DISPEL     = 0x0400,  // removes harmful auras from allies
    KIND_INTERRUPT  = 0x0800,
    KIND_CONTROL    = 0x1000,  // stuns, fears, polymorphs... never aimed at a group member
    KIND_STANCE     = 0x2000,  // a form or stance on the caster that never expires
    KIND_RESURRECT  = 0x4000   // brings a dead ally back
};

/*
 * Class abilities, indexed by class id, ordered by required level.
 *
 * Most CoA abilities are scripted (dummy or script effects), so what mod-ascension-compat says
 * a class can learn is the only reliable list of what it can cast.
 */
struct CoaAbility
{
    uint32 spellId;
    uint8 requiredLevel;
    uint16 kind;
    uint32 dispelMask;
    uint32 firstSpellId;  // first rank: ranks of one spell replace each other
};

struct ClassKit
{
    std::vector<CoaAbility> abilities;
    uint16 kinds = 0;  // every kind some ability of the class has
};

// Ally targets: pet, party and raid areas, ally or any unit, chain heal.
bool IsAllyTarget(uint32 target)
{
    switch (target)
    {
        case 5: case 20: case 21: case 25: case 30: case 31: case 33:
        case 34: case 35: case 37: case 45: case 56: case 57: case 61:
            return true;
        default:
            return false;
    }
}

bool IsAllyAreaTarget(uint32 target)
{
    switch (target)
    {
        case 20: case 30: case 31: case 33: case 34: case 37: case 45: case 56: case 61:
            return true;
        default:
            return false;
    }
}

// Aimed at one chosen ally, who may be someone else than the caster.
bool IsAllyCastTarget(uint32 target)
{
    return target == TARGET_UNIT_TARGET_ALLY || target == TARGET_UNIT_TARGET_ANY || target == 35 ||
           target == TARGET_UNIT_TARGET_CHAINHEAL_ALLY || target == TARGET_UNIT_TARGET_RAID;
}

bool IsEnemyAreaTarget(uint32 target)
{
    switch (target)
    {
        case TARGET_UNIT_SRC_AREA_ENEMY: case TARGET_UNIT_DEST_AREA_ENEMY: case TARGET_UNIT_CONE_ENEMY_24:
        case TARGET_DEST_DYNOBJ_ENEMY: case TARGET_UNIT_CONE_ENEMY_54: case TARGET_UNIT_CONE_ENEMY_104:
            return true;
        default:
            return false;
    }
}

bool IsEnemyTarget(uint32 target)
{
    return target == TARGET_UNIT_TARGET_ENEMY || target == TARGET_DEST_TARGET_ENEMY || IsEnemyAreaTarget(target);
}

bool IsAuraEffect(SpellEffectInfo const& effect)
{
    return effect.Effect == SPELL_EFFECT_APPLY_AURA || effect.Effect == SPELL_EFFECT_APPLY_AREA_AURA_PARTY ||
           effect.Effect == SPELL_EFFECT_APPLY_AREA_AURA_RAID;
}

bool IsDamage(SpellEffectInfo const& effect)
{
    switch (effect.Effect)
    {
        case SPELL_EFFECT_SCHOOL_DAMAGE: case SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL: case SPELL_EFFECT_WEAPON_DAMAGE:
        case SPELL_EFFECT_NORMALIZED_WEAPON_DMG: case SPELL_EFFECT_WEAPON_PERCENT_DAMAGE:
        case SPELL_EFFECT_HEALTH_LEECH: case SPELL_EFFECT_POWER_BURN:
            return true;
        default:
            break;
    }

    return IsAuraEffect(effect) &&
           (effect.ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE || effect.ApplyAuraName == SPELL_AURA_PERIODIC_LEECH ||
            effect.ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE_PERCENT);
}

bool IsControlAura(SpellEffectInfo const& effect)
{
    switch (effect.ApplyAuraName)
    {
        case SPELL_AURA_MOD_CONFUSE: case SPELL_AURA_MOD_FEAR: case SPELL_AURA_MOD_STUN: case SPELL_AURA_MOD_ROOT:
        case SPELL_AURA_MOD_SILENCE: case SPELL_AURA_MOD_PACIFY: case SPELL_AURA_TRANSFORM:
        case SPELL_AURA_MOD_PACIFY_SILENCE:
            return true;
        default:
            return false;
    }
}

bool IsDefensiveAura(SpellEffectInfo const& effect)
{
    switch (effect.ApplyAuraName)
    {
        case SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN:
            return effect.BasePoints < 0;
        case SPELL_AURA_SCHOOL_ABSORB: case SPELL_AURA_MOD_PARRY_PERCENT: case SPELL_AURA_MOD_DODGE_PERCENT:
        case SPELL_AURA_MOD_BLOCK_PERCENT: case SPELL_AURA_MOD_INCREASE_HEALTH:
        case SPELL_AURA_MOD_INCREASE_HEALTH_PERCENT: case SPELL_AURA_SCHOOL_IMMUNITY:
            return true;
        default:
            return false;
    }
}

/*
 * A stance, form or aspect: every effect is an aura on the caster and it never runs out.
 * The bot takes one out of combat and keeps it; it has no place in the damage rotation.
 * Several CoA abilities are gated behind one through CasterAuraSpell (Beetle Form 803183
 * carries 64 of them, Spider Form 800841 another 42), so they cannot simply be ignored.
 */
bool IsStance(SpellInfo const* info)
{
    if (info->GetMaxDuration() > 0)
        return false;

    bool aura = false;
    for (SpellEffectInfo const& effect : info->Effects)
    {
        if (!effect.IsEffect())
            continue;

        // A summon, a teleport, a trade skill or an item alongside the aura: not a stance.
        if (!IsAuraEffect(effect) || effect.ApplyAuraName == SPELL_AURA_NONE ||
            effect.TargetA.GetTarget() != TARGET_UNIT_CASTER)
            return false;

        aura = true;
    }

    return aura;
}

// What a spell does, looking two levels into the spells it triggers: CoA abilities often
// carry their heal, taunt or aura in a triggered spell.
void Classify(SpellInfo const* info, CoaAbility& ability, uint8 depth = 0)
{
    constexpr int32 LongAura = 5 * MINUTE * IN_MILLISECONDS;
    int32 const duration = info->GetMaxDuration();

    // Only the ability itself: a spell it triggers is not the stance the bot stands in.
    if (!depth && IsStance(info))
        ability.kind |= KIND_STANCE;

    for (SpellEffectInfo const& effect : info->Effects)
    {
        if (!effect.IsEffect())
            continue;

        uint32 const targetA = effect.TargetA.GetTarget();
        uint32 const targetB = effect.TargetB.GetTarget();
        bool const aura = IsAuraEffect(effect);
        bool const self = targetA == TARGET_UNIT_CASTER;
        bool const ally = IsAllyTarget(targetA) || IsAllyTarget(targetB);
        bool const enemy = IsEnemyTarget(targetA) || IsEnemyTarget(targetB);

        if (IsAllyCastTarget(targetA))
            ability.kind |= KIND_ALLY_CAST;

        if (enemy)
        {
            ability.kind |= KIND_HOSTILE;
            if (IsEnemyAreaTarget(targetA) || IsEnemyAreaTarget(targetB))
                ability.kind |= KIND_AOE;
        }

        if (IsDamage(effect))
            ability.kind |= KIND_DAMAGE;

        if (effect.Effect == SPELL_EFFECT_RESURRECT || effect.Effect == SPELL_EFFECT_RESURRECT_NEW)
            ability.kind |= KIND_RESURRECT;

        // Some CoA crowd control also cleanses (Babify, Knockout): a bot must not "dispel" a
        // group member by stunning or transforming it.
        if (aura && !self && IsControlAura(effect))
            ability.kind |= KIND_CONTROL;

        bool const periodicHeal = aura && effect.ApplyAuraName == SPELL_AURA_PERIODIC_HEAL;
        bool const heal = effect.Effect == SPELL_EFFECT_HEAL || effect.Effect == SPELL_EFFECT_HEAL_PCT ||
                          effect.Effect == SPELL_EFFECT_HEAL_MAX_HEALTH || periodicHeal;
        if (heal && ally)
        {
            ability.kind |= KIND_HEAL;
            if (IsAllyAreaTarget(targetA) || IsAllyAreaTarget(targetB))
                ability.kind |= KIND_GROUP_HEAL;
            if (periodicHeal)
                ability.kind |= KIND_HOT;
        }

        if (effect.Effect == SPELL_EFFECT_ATTACK_ME || (aura && effect.ApplyAuraName == SPELL_AURA_MOD_TAUNT))
            ability.kind |= KIND_TAUNT;

        if (effect.Effect == SPELL_EFFECT_DISPEL && (ally || self))
        {
            ability.kind |= KIND_DISPEL;
            ability.dispelMask |= SpellInfo::GetDispelMask(DispelType(effect.MiscValue));
        }

        if (effect.Effect == SPELL_EFFECT_INTERRUPT_CAST ||
            (aura && enemy && effect.ApplyAuraName == SPELL_AURA_MOD_SILENCE))
            ability.kind |= KIND_INTERRUPT;

        if (aura && self && IsDefensiveAura(effect) && duration > 0 && duration < LongAura)
            ability.kind |= KIND_DEFENSIVE;

        // Stances and forms (no duration) are left out: two of them would take turns forever.
        if (aura && (self || ally) && !heal && info->IsPositive() && duration >= LongAura &&
            effect.ApplyAuraName != SPELL_AURA_MOD_SHAPESHIFT)
            ability.kind |= KIND_BUFF;

        // A proc rider is not part of the cast: "when you hit, deal nature damage" describes
        // the aura, not the spell that applies it. Following it made a weapon poison or a
        // damage-proc buff look like a damage spell and sent it to the damage rotation, where
        // it was reapplied over and over (Blight Venom 805776, Temporal Resilience 680389).
        // Triggers the cast itself fires are still followed.
        bool const proc = aura && (effect.ApplyAuraName == SPELL_AURA_PROC_TRIGGER_SPELL ||
                                   effect.ApplyAuraName == SPELL_AURA_PROC_TRIGGER_SPELL_WITH_VALUE ||
                                   effect.ApplyAuraName == SPELL_AURA_PROC_TRIGGER_DAMAGE ||
                                   effect.ApplyAuraName == SPELL_AURA_ADD_TARGET_TRIGGER);

        if (effect.TriggerSpell && !proc && depth < 2)
            if (SpellInfo const* triggered = sSpellMgr->GetSpellInfo(effect.TriggerSpell))
                Classify(triggered, ability, depth + 1);
    }
}

std::unordered_map<uint8, ClassKit> const& ClassAbilities()
{
    // Magic static: loaded once, thread safe, on the first bot that needs it.
    static std::unordered_map<uint8, ClassKit> const abilities = []
    {
        std::unordered_map<uint8, ClassKit> byClass;
        std::unordered_map<uint8, std::unordered_map<uint32, size_t>> known;

        auto add = [&byClass, &known](uint8 classId, AscensionClassAbility const& learnable)
        {
            ClassKit& kit = byClass[classId];
            auto const [itr, inserted] = known[classId].try_emplace(learnable.SpellId, kit.abilities.size());
            if (!inserted)
            {
                // Listed again as a higher rank of another spell: remember the link.
                if (learnable.FirstSpellId != learnable.SpellId)
                    kit.abilities[itr->second].firstSpellId = learnable.FirstSpellId;
                return false;
            }

            CoaAbility ability = { learnable.SpellId, learnable.RequiredLevel, 0, 0, learnable.FirstSpellId };
            if (SpellInfo const* info = sSpellMgr->GetSpellInfo(learnable.SpellId))
                Classify(info, ability);

            kit.abilities.push_back(ability);
            kit.kinds |= ability.kind;
            return true;
        };

        // Class grants, Character Advancement entries (a bot only knows those of its own
        // specialization, which HasSpell sorts out at run time) and higher ranks. Ids below
        // 100000 are vanilla weapon skills and Auto Attack.
        uint32 count = 0, ranks = 0;
        for (uint8 classId = 1; classId < MAX_CLASSES; ++classId)
        {
            if (!IsAscensionCustomClassId(classId))
                continue;

            for (AscensionClassAbility const& learnable : GetAscensionClassAbilities(classId))
            {
                if (learnable.SpellId < 100000 || !add(classId, learnable))
                    continue;

                ++count;
                ranks += learnable.SpellId != learnable.FirstSpellId;
            }
        }

        uint32 heals = 0, aoe = 0, buffs = 0, defensives = 0, dispels = 0, interrupts = 0;
        for (auto& [classId, kit] : byClass)
        {
            std::stable_sort(kit.abilities.begin(), kit.abilities.end(),
                [](CoaAbility const& a, CoaAbility const& b) { return a.requiredLevel < b.requiredLevel; });

            for (CoaAbility const& ability : kit.abilities)
            {
                heals += (ability.kind & KIND_HEAL) != 0;
                aoe += (ability.kind & KIND_AOE) != 0;
                buffs += (ability.kind & KIND_BUFF) != 0;
                defensives += (ability.kind & KIND_DEFENSIVE) != 0;
                dispels += (ability.kind & KIND_DISPEL) != 0;
                interrupts += (ability.kind & KIND_INTERRUPT) != 0;
            }
        }

        LOG_INFO("playerbots", "coa: {} abilities ({} higher ranks) from mod-ascension-compat, {} classes",
                 count, ranks, byClass.size());
        LOG_INFO("playerbots", "coa: {} heals, {} area attacks, {} buffs, {} defensives, {} dispels, {} interrupts",
                 heals, aoe, buffs, defensives, dispels, interrupts);
        return byClass;
    }();

    return abilities;
}

// Whether the bot's class has any ability of that kind: a cheap test before looking at spells.
bool ClassHas(Player* bot, uint16 kind)
{
    auto const& all = ClassAbilities();
    auto const found = all.find(bot->getClass());
    return found != all.end() && (found->second.kinds & kind);
}

struct Usable
{
    SpellInfo const* info;
    uint16 kind;
    uint32 dispelMask;
};

// Active abilities the bot has reached and actually knows, for which `wanted(kind)` is true.
template <typename Filter>
std::vector<Usable> KnownAbilities(Player* bot, Filter wanted)
{
    std::vector<Usable> usable;

    auto const& all = ClassAbilities();
    auto const found = all.find(bot->getClass());
    if (found == all.end())
        return usable;

    // A bot can still know the lower ranks of a spell: only cast the highest one it has reached.
    // Abilities are ordered by required level, so a later rank replaces an earlier one.
    std::unordered_map<uint32, size_t> rankIndex;
    for (CoaAbility const& ability : found->second.abilities)
    {
        if (!wanted(ability.kind) || ability.requiredLevel > bot->GetLevel() || !bot->HasSpell(ability.spellId))
            continue;

        // Class grants include passives (e.g. 552011 Resilient Constitution).
        SpellInfo const* info = sSpellMgr->GetSpellInfo(ability.spellId);
        if (!info || info->IsPassive())
            continue;

        auto const [itr, inserted] = rankIndex.try_emplace(ability.firstSpellId, usable.size());
        if (inserted)
            usable.push_back({ info, ability.kind, ability.dispelMask });
        else
            usable[itr->second] = { info, ability.kind, ability.dispelMask };
    }

    return usable;
}

bool IsFriendlyDispel(uint16 kind)
{
    return (kind & KIND_DISPEL) && !(kind & (KIND_CONTROL | KIND_HOSTILE));
}

// Whether the bot knows an ability for which `wanted(kind)` is true and that is off cooldown:
// actions only run when they have something to cast, which keeps the usage counters honest.
template <typename Filter>
bool HasReadyAbility(PlayerbotAI* botAI, Player* bot, Filter wanted)
{
    auto const& benched = static_cast<CoaAiObjectContext*>(botAI->GetAiObjectContext())->benchedSpells;
    time_t const now = time(nullptr);

    for (Usable const& spell : KnownAbilities(bot, wanted))
    {
        auto const bench = benched.find(spell.info->Id);
        if (!bot->HasSpellCooldown(spell.info->Id) && (bench == benched.end() || bench->second <= now))
            return true;
    }

    return false;
}

// Abilities worth using in the damage rotation.
bool IsAttack(uint16 kind, bool tank)
{
    // An ability none of the tests above recognised is a summon, a stance, a permanent self
    // aura, a trade skill or an item: over the whole CoA catalogue, 645 active abilities end
    // up here and not one of them carries a target the caster could aim at an enemy. Trying
    // them on the target only spent global cooldowns and, for the permanent ones, reapplied
    // the same aura for ever (Bushcraft 800267, Serpent Ward 500960, Tower Formation 800317).
    if (!kind)
        return false;

    // Heals, buffs, defensives and dispels have their own actions.
    if (!(kind & (KIND_HOSTILE | KIND_DAMAGE)))
        return false;

    if (kind & KIND_DAMAGE)
        return true;

    // A taunt that deals no damage pulls aggro off the tank: only tanks use it. Interrupts
    // are kept for enemy casts.
    if (kind & KIND_TAUNT)
        return tank;

    return !(kind & KIND_INTERRUPT);
}

/*
 * The check CastSpell will actually face.
 *
 * PlayerbotAI::CanCastSpell checks with TRIGGERED_IGNORE_POWER_AND_REAGENT_COST and
 * accepts OUT_OF_RANGE, MOVING and NOT_INFRONT as success, whereas CastSpell prepares
 * with TRIGGERED_NONE. A spell can therefore pass the first and be refused by the second.
 */
SpellCastResult StrictCheck(Player* bot, SpellInfo const* info, Unit* target)
{
    ObjectGuid const oldSel = bot->GetTarget();

    // Spell::CheckCast reads m_powerCost, and that is only worked out in Spell::prepare: a check run
    // on its own therefore sees a spell that costs nothing and lets every spell through, however
    // empty the bot's mana or custom resource is. The cast then fails with SPELL_FAILED_NO_POWER -
    // 186 of the 210 refusals measured on 18/09. So pay for it here, before asking.
    if (info->PowerType < MAX_POWERS && info->PowerType != POWER_HEALTH)
    {
        int32 const cost = info->CalcPowerCost(bot, info->GetSchoolMask());
        if (cost > 0 && bot->GetPower(Powers(info->PowerType)) < cost)
            return SPELL_FAILED_NO_POWER;
    }

    Spell* spell = new Spell(bot, info, TRIGGERED_NONE);
    spell->m_targets.SetUnitTarget(target);
    // Ground-targeted spells: CastSpell aims them at the target's position.
    if (info->Targets & TARGET_FLAG_DEST_LOCATION)
        spell->m_targets.SetDst(*target);
    SpellCastResult const result = spell->CheckCast(true);
    delete spell;

    bot->SetSelection(oldSel);
    return result;
}

constexpr uint16 FAILURE_REFUSED = 1000;  // passed the strict check, refused by PlayerbotAI::CastSpell
constexpr uint16 FAILURE_NOTHING = 1001;  // nothing left to cast (e.g. every heal over time already on the target)
constexpr uint16 FAILURE_MOVING = 1002;   // cast time while moving: the bot stops and casts on a later tick
constexpr uint16 FAILURE_SITTING = 1003;  // refused while sitting (eating, drinking): the bot stands up first
constexpr uint16 FAILURE_CASTING = 1004;  // refused while still casting a heal or buff
constexpr uint16 SKIPPED_COOLDOWN = 2000;  // not tried: on cooldown (group fight log only)
constexpr uint16 SKIPPED_BENCHED = 2001;   // not tried: set aside after an earlier failure (group fight log only)

// A bot busy casting an attack drops it for a heal, dispel, defensive, taunt or interrupt. A heal
// or buff in progress is kept, or heals would keep cutting each other off.
void DropAttackCast(Player* bot)
{
    for (CurrentSpellTypes type : { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL })
        if (Spell* current = bot->GetCurrentSpell(type))
            if (!current->GetSpellInfo()->IsPositive())
                bot->InterruptSpell(type, false);
}

// Whether the bot is holding its mana back for healing rather than spending it on damage.
// A healer keeps the larger share; any other bot that knows a heal keeps a smaller cushion, so
// that it can still patch itself up; a bot with no heal at all never holds anything back, as it
// would stop fighting for nothing. Both shares are settings, 0 turning the reserve off.
bool SmartHeal();
Player* GroupTank(Player* bot);
// In a group with a tank, a healer is there to heal: it keeps its mana above this share and fights
// with what costs nothing, where alone it only kept AiPlayerbot.CoaHealerManaReserve.
constexpr uint32 GroupHealerManaReserve = 85;

bool SavingManaForHeals(Player* bot)
{
    if (bot->getPowerType() != POWER_MANA)
        return false;

    // A healer whose damage is its healing (Cultist Heretic) holds nothing back: keeping its mana
    // from damage would keep it from healing. AiPlayerbot.CoaOffensiveHealerSpecs.
    if (uint32 const specialization = GetAscensionActiveSpecialization(bot))
        if (sPlayerbotAIConfig.coaOffensiveHealerSpecs.count(specialization))
            return false;

    uint32 reserve = GetCoaRole(bot) == CoaRole::Heal ? sPlayerbotAIConfig.coaHealerManaReserve
                                                      : sPlayerbotAIConfig.coaCasterManaReserve;
    if (SmartHeal() && GetCoaRole(bot) == CoaRole::Heal && GroupTank(bot))
        reserve = std::max(reserve, GroupHealerManaReserve);
    if (!reserve || bot->GetPowerPct(POWER_MANA) >= float(reserve))
        return false;

    return !KnownAbilities(bot, [](uint16 kind)
        { return (kind & (KIND_HEAL | KIND_HOT)) && !(kind & (KIND_CONTROL | KIND_HOSTILE)); }).empty();
}

// Removes what costs mana, for a bot that is keeping the rest of its mana for healing.
void DropManaSpells(Player* bot, std::vector<Usable>& spells)
{
    spells.erase(std::remove_if(spells.begin(), spells.end(), [bot](Usable const& spell)
        { return spell.info->PowerType == POWER_MANA && spell.info->CalcPowerCost(bot, spell.info->GetSchoolMask()) > 0; }),
        spells.end());
}

// Puts the cheapest heals first. Low on mana a bot would otherwise keep offering its biggest heal,
// be turned down for want of power and heal nobody, while a small heal was still within reach.
void CheapestFirst(Player* bot, std::vector<Usable>& spells)
{
    std::stable_sort(spells.begin(), spells.end(), [bot](Usable const& a, Usable const& b)
    {
        return a.info->CalcPowerCost(bot, a.info->GetSchoolMask()) <
               b.info->CalcPowerCost(bot, b.info->GetSchoolMask());
    });
}

/*
 * Healing in a group the way a player would (AiPlayerbot.CoaSmartHeal): whoever is dropping first,
 * the tank before the others, the heal that fits the need, from close enough to the tank to reach
 * anyone who pulls aggro. Measured on a Scarlet Monastery run before it existed: the healer kept the
 * tank up (72% of the healing, 0.5 s under 25%) but left the player 11.5 s under 25%, from up to
 * 39 yards away, while 46% of the fight something other than the tank was being hit.
 */
bool SmartHeal() { return sPlayerbotAIConfig.coaSmartHeal; }

bool OnSameInstance(Player* a, Player* b)
{
    return a->IsInWorld() && b->IsInWorld() && a->GetMapId() == b->GetMapId() && a->GetInstanceId() == b->GetInstanceId();
}

// The living tank of the bot's group on its own map instance, other than the bot itself.
Player* GroupTank(Player* bot)
{
    Group* group = bot->GetGroup();
    if (!group)
        return nullptr;

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        if (Player* member = ref->GetSource())
            if (member != bot && member->IsAlive() && OnSameInstance(bot, member) &&
                PlayerbotAI::IsTank(member))
                return member;

    return nullptr;
}

/*
 * Below what health a healer tops a group member up, from its mana. CoA heals cost 15 to 39% of
 * base mana each - the same as on Ascension - so a healer that heals everyone under 85% is dry
 * after a handful of casts: in the healer trial of 21/09 all eight classes were, with 65 to 76% of
 * their healing lost to overheal. Full, it heals under 85%; the emptier it gets, the more it lets a
 * scratch go, down to 45% at a fifth of its mana. Someone under the critical line is healed anyway.
 */
float HealLine(Player* bot)
{
    float const low = float(sPlayerbotAIConfig.lowHealth);
    float const high = float(sPlayerbotAIConfig.almostFullHealth);
    if (!SmartHeal() || bot->getPowerType() != POWER_MANA)
        return high;

    float const share = std::clamp((bot->GetPowerPct(POWER_MANA) - 20.0f) / 60.0f, 0.0f, 1.0f);
    return low + (high - low) * share;
}

// Whether another member of the group is already casting a heal on this one.
bool BeingHealedByAnother(Player* bot, Unit* target)
{
    Group* group = bot->GetGroup();
    if (!group)
        return false;

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == bot || !OnSameInstance(bot, member))
            continue;
        Spell const* spell = member->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (spell && spell->m_targets.GetUnitTargetGUID() == target->GetGUID() &&
            (spell->GetSpellInfo()->HasEffect(SPELL_EFFECT_HEAL) || spell->GetSpellInfo()->HasEffect(SPELL_EFFECT_HEAL_PCT)))
            return true;
    }
    return false;
}

// The member to heal, of those under `below` percent: under the critical threshold before anyone
// else, then the lowest health with the tank counted 15 points lower, as the one taking the hits.
// One another healer is already healing is left to it unless dropping; for a heal over time, one
// already carrying a heal over time too, unless under the medium line.
Unit* SmartHealTarget(Player* bot, float below, bool overTime = false)
{
    Group* group = bot->GetGroup();
    if (!group)
        return bot->GetHealthPct() < below ? bot : nullptr;

    Unit* best = nullptr;
    float bestScore = 1000.0f;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        // Within reach only: a member chosen further away made every heal fail out of range, and
        // nothing brought the healer closer (a Witch Doctor's Loa's Brew, 5 times in one fight). One
        // further away is left to the generic "reach party member to heal", which walks to it.
        if (!member || member->IsGameMaster() || !member->IsAlive() || !OnSameInstance(bot, member) ||
            member->IsCharmed() || bot->GetDistance2d(member) > sPlayerbotAIConfig.healDistance)
            continue;

        float const health = member->GetHealthPct();
        if (health >= below)
            continue;

        // Last, as the costliest test: a raycast, for the members that need healing only.
        if (!bot->IsWithinLOSInMap(member))
            continue;

        bool const critical = health < sPlayerbotAIConfig.criticalHealth;
        if (!critical && BeingHealedByAnother(bot, member))
            continue;
        if (overTime && health >= sPlayerbotAIConfig.mediumHealth && member->HasAuraType(SPELL_AURA_PERIODIC_HEAL))
            continue;

        float score = health;
        if (PlayerbotAI::IsTank(member))
            score -= 15.0f;
        if (health < sPlayerbotAIConfig.criticalHealth)
            score -= 40.0f;

        if (score < bestScore)
        {
            best = member;
            bestScore = score;
        }
    }
    return best;
}

// Rough healing of one cast before bonuses: direct heals, plus every tick of a heal over time.
// Enough to tell a small heal from a large one; CoA's scripted heals may read as 0.
float HealAmount(Player* bot, SpellInfo const* info, Unit* target)
{
    float total = 0.0f;
    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
    {
        SpellEffectInfo const& effect = info->Effects[i];
        if (effect.Effect == SPELL_EFFECT_HEAL)
            total += float(std::max(0, effect.CalcValue(bot)));
        else if (effect.Effect == SPELL_EFFECT_HEAL_PCT)
            total += target->GetMaxHealth() * float(std::max(0, effect.CalcValue(bot))) / 100.0f;
        else if (effect.Effect == SPELL_EFFECT_APPLY_AURA && effect.ApplyAuraName == SPELL_AURA_PERIODIC_HEAL)
        {
            int32 const duration = info->GetMaxDuration();
            uint32 const ticks = effect.Amplitude > 0 && duration > 0 ? uint32(duration / effect.Amplitude) : 1;
            total += float(std::max(0, effect.CalcValue(bot))) * std::max(1u, ticks);
        }
    }
    return total;
}

// Orders single target heals for how urgent the need is. Someone dropping gets the fastest heal
// first, the largest of equal speed; otherwise the largest heal that does not overshoot what is
// missing by more than a fifth comes first, then the smaller ones, and the oversized ones last.
// Heals over time stay behind direct heals and area heals behind both, as before.
void OrderByUrgency(Player* bot, Unit* target, std::vector<Usable>& spells)
{
    bool const critical = target->GetHealthPct() < sPlayerbotAIConfig.criticalHealth;
    float const missing = float(target->GetMaxHealth() - target->GetHealth());

    struct Scored
    {
        Usable spell;
        uint8 rank;
        uint32 castTime;
        float amount;
    };
    std::vector<Scored> scored;
    scored.reserve(spells.size());
    for (Usable const& spell : spells)
        scored.push_back({ spell, uint8(((spell.kind & KIND_GROUP_HEAL) ? 2 : 0) + ((spell.kind & KIND_HOT) ? 1 : 0)),
                           uint32(spell.info->CalcCastTime(bot)), HealAmount(bot, spell.info, target) });

    std::stable_sort(scored.begin(), scored.end(), [critical, missing](Scored const& a, Scored const& b)
    {
        if (a.rank != b.rank)
            return a.rank < b.rank;
        if (critical)
        {
            if (a.castTime != b.castTime)
                return a.castTime < b.castTime;
            return a.amount > b.amount;
        }
        bool const aFits = a.amount <= missing * 1.2f;
        bool const bFits = b.amount <= missing * 1.2f;
        if (aFits != bFits)
            return aFits;
        return aFits ? a.amount > b.amount : a.amount < b.amount;
    });

    for (std::size_t i = 0; i < spells.size(); ++i)
        spells[i] = scored[i].spell;
}

// How far from the tank a healer stands in a fight: close enough to reach anyone the tank loses a
// mob to, far enough to stay out of what hits the tank.
constexpr float StayNearTank = 18.0f;

constexpr time_t SpellBenchSeconds = 20;
constexpr time_t RefusedBenchSeconds = 8;
constexpr time_t NoPowerBenchSeconds = 5;

// Failures that will not clear up on the next tick: wrong target or state for this spell.
bool IsLastingFailure(SpellCastResult result)
{
    switch (result)
    {
        case SPELL_FAILED_BAD_IMPLICIT_TARGETS: case SPELL_FAILED_BAD_TARGETS: case SPELL_FAILED_CASTER_AURASTATE:
        case SPELL_FAILED_NOT_SHAPESHIFT: case SPELL_FAILED_ONLY_SHAPESHIFT: case SPELL_FAILED_TARGET_AURASTATE:
        case SPELL_FAILED_EQUIPPED_ITEM_CLASS: case SPELL_FAILED_REAGENTS: case SPELL_FAILED_TOTEMS:
            return true;
        default:
            return false;
    }
}
void RecordFailure(uint8 kind, uint32 spellId, uint16 reason);

// Casts the first ability of the list that passes the strict check on the target. Returns it,
// or nullptr when none went off. With a usage kind, why each ability failed is counted.
SpellInfo const* CastFirst(PlayerbotAI* botAI, Player* bot, std::vector<Usable> const& spells, Unit* target,
                           uint8 usage = 255)
{
    time_t const now = time(nullptr);
    auto& benched = static_cast<CoaAiObjectContext*>(botAI->GetAiObjectContext())->benchedSpells;
    // Heals are followed spell by spell in a measured group fight, skipped ones included.
    // USAGE_HEAL, USAGE_GROUP_HEAL and USAGE_HOT, whose enum comes further down.
    bool const healing = usage == 2 || usage == 3 || usage == 4;
    auto note = [bot, healing](uint32 spellId, uint16 outcome)
    {
        if (healing)
            CoaTelemetryNoteHeal(bot, spellId, outcome);
    };

    for (Usable const& spell : spells)
    {
        // A spell on cooldown would only fail with SPELL_FAILED_NOT_READY.
        if (bot->HasSpellCooldown(spell.info->Id))
        {
            note(spell.info->Id, SKIPPED_COOLDOWN);
            continue;
        }

        // The global cooldown blocks every spell alike: try again on a later tick.
        if (bot->GetGlobalCooldownMgr().HasGlobalCooldown(spell.info))
            return nullptr;

        auto const bench = benched.find(spell.info->Id);
        if (bench != benched.end())
        {
            if (bench->second > now)
            {
                note(spell.info->Id, SKIPPED_BENCHED);
                continue;
            }
            benched.erase(bench);
        }

        SpellCastResult const check = StrictCheck(bot, spell.info, target);
        if (check == SPELL_CAST_OK)
        {
            // PlayerbotAI::CastSpell refuses a spell with a cast time while the bot moves: stop
            // now and cast it on a later tick, once standing still.
            if (bot->isMoving() && spell.info->CalcCastTime(bot))
            {
                bot->StopMoving();
                if (usage != 255)
                    RecordFailure(usage, spell.info->Id, FAILURE_MOVING);
                note(spell.info->Id, FAILURE_MOVING);
                continue;
            }

            if (usage != 255)
                DropAttackCast(bot);

            bool const sitting = !bot->IsStandState();
            bool const casting = bot->IsNonMeleeSpellCast(false, true, true);
            if (botAI->CastSpell(spell.info->Id, target))
            {
                note(spell.info->Id, 0);
                return spell.info;
            }

            // Refused for a reason the check cannot see (CoA spell scripts check their own
            // resources when the cast is prepared): leave it aside briefly so the next ability
            // of the list gets its turn instead of this one failing every tick.
            if (!sitting && !casting)
                benched[spell.info->Id] = now + RefusedBenchSeconds;

            if (usage != 255)
                RecordFailure(usage, spell.info->Id, sitting ? FAILURE_SITTING : casting ? FAILURE_CASTING : FAILURE_REFUSED);
            note(spell.info->Id, sitting ? FAILURE_SITTING : casting ? FAILURE_CASTING : FAILURE_REFUSED);
            continue;
        }
        // A heal that cannot be cast in a form (most Venomancer heals) while the healer stands in
        // one: it leaves the form and heals on the next tick, instead of setting its heals aside.
        else if (check == SPELL_FAILED_NOT_SHAPESHIFT && healing && bot->HasAuraType(SPELL_AURA_MOD_SHAPESHIFT))
            bot->RemoveAurasByType(SPELL_AURA_MOD_SHAPESHIFT);
        else if (check == SPELL_FAILED_CASTER_AURASTATE && spell.info->CasterAuraSpell)
        {
            // Waiting on its marker (see CoaHealAction::AddPrerequisites): not set aside.
        }
        else if (IsLastingFailure(check))
            benched[spell.info->Id] = now + SpellBenchSeconds;
        // Out of mana, energy or rage: asking again on the very next tick changes nothing, and
        // with the spell set aside the action reports itself useless, so the bot does something
        // it can afford instead of spending its ticks being turned down.
        else if (check == SPELL_FAILED_NO_POWER)
            benched[spell.info->Id] = now + NoPowerBenchSeconds;

        if (usage != 255)
            RecordFailure(usage, spell.info->Id, uint16(check));
        note(spell.info->Id, uint16(check));
    }

    return nullptr;
}

/*
 * Usage counters of the CoA actions, for all bots, written every 10 minutes to the
 * "playerbots.coa" logger (CoaBots.log): how often each action was tried (its trigger fired)
 * and how often it actually cast something, with the most cast interrupts and dispels.
 */
enum UsageKind : uint8
{
    USAGE_ATTACK, USAGE_AOE, USAGE_HEAL, USAGE_GROUP_HEAL, USAGE_HOT, USAGE_TAUNT,
    USAGE_DEFENSIVE, USAGE_DISPEL, USAGE_INTERRUPT, USAGE_BUFF, USAGE_MAX
};

constexpr char const* UsageNames[USAGE_MAX] =
{
    "attack", "aoe", "heal", "group heal", "hot", "taunt", "defensive", "dispel", "interrupt", "buff"
};

struct UsageCounter
{
    std::atomic<uint64> tried{ 0 };
    std::atomic<uint64> cast{ 0 };
};

std::array<UsageCounter, USAGE_MAX> Usage;
std::mutex UsageSpellsLock;
std::map<uint32, uint32> UsageSpells[USAGE_MAX];  // spell id -> casts, dispels and interrupts only
std::atomic<time_t> UsageLastReport{ 0 };
// (spell id, reason) -> failures, for the kinds whose casts are diagnosed
std::map<std::pair<uint32, uint16>, uint32> UsageFailures[USAGE_MAX];

void RecordFailure(uint8 kind, uint32 spellId, uint16 reason)
{
    if (kind >= USAGE_MAX)
        return;

    std::lock_guard<std::mutex> guard(UsageSpellsLock);
    ++UsageFailures[kind][{ spellId, reason }];
}

void ReportUsage(time_t now)
{
    std::string line;
    for (uint8 kind = 0; kind < USAGE_MAX; ++kind)
        line += Acore::StringFormat("{}{} {}/{}", kind ? ", " : "", UsageNames[kind],
                                    Usage[kind].cast.load(), Usage[kind].tried.load());
    LOG_INFO("playerbots.coa", "coa usage since start (cast/tried): {}", line);

    std::lock_guard<std::mutex> guard(UsageSpellsLock);
    // Attacks and heals list more spells: they show which rank of each spell the bots cast.
    for (UsageKind kind : { USAGE_DISPEL, USAGE_INTERRUPT, USAGE_ATTACK, USAGE_HEAL })
    {
        std::vector<std::pair<uint32, uint32>> top(UsageSpells[kind].begin(), UsageSpells[kind].end());
        std::sort(top.begin(), top.end(), [](auto const& a, auto const& b) { return a.second > b.second; });
        size_t const shown = kind == USAGE_ATTACK || kind == USAGE_HEAL ? 40 : 15;
        std::string spells;
        for (size_t i = 0; i < top.size() && i < shown; ++i)
        {
            SpellInfo const* info = sSpellMgr->GetSpellInfo(top[i].first);
            spells += Acore::StringFormat("{}{} ({}) x{}", i ? ", " : "", info ? info->SpellName[0] : "?",
                                          top[i].first, top[i].second);
        }
        LOG_INFO("playerbots.coa", "coa {} spells: {}", UsageNames[kind], spells.empty() ? "none yet" : spells);
    }

    // Why heals and taunts fail: spell (id) reason xcount. Reasons are SpellCastResult values,
    // "refused" when CastSpell turned down a spell the check accepted, "nothing" when no
    // ability was left to try.
    for (UsageKind kind : { USAGE_HEAL, USAGE_GROUP_HEAL, USAGE_HOT, USAGE_TAUNT, USAGE_DISPEL })
    {
        std::vector<std::pair<std::pair<uint32, uint16>, uint32>> top(UsageFailures[kind].begin(), UsageFailures[kind].end());
        if (top.empty())
            continue;

        std::sort(top.begin(), top.end(), [](auto const& a, auto const& b) { return a.second > b.second; });
        std::string failures;
        for (size_t i = 0; i < top.size() && i < 10; ++i)
        {
            auto const [spellId, reason] = top[i].first;
            SpellInfo const* info = spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
            std::string const why = reason == FAILURE_REFUSED ? "refused"
                                  : reason == FAILURE_NOTHING ? "nothing"
                                  : reason == FAILURE_MOVING ? "moving"
                                  : reason == FAILURE_SITTING ? "sitting"
                                  : reason == FAILURE_CASTING ? "casting"
                                  : std::to_string(reason);
            failures += Acore::StringFormat("{}{} ({}) {} x{}", i ? ", " : "", info ? info->SpellName[0] : "-",
                                            spellId, why, top[i].second);
        }
        LOG_INFO("playerbots.coa", "coa {} failures: {}", UsageNames[kind], failures);
    }
    (void)now;
}

// Counts one run of an action and, when `spell` is set, the cast it made.
SpellInfo const* RecordUsage(UsageKind kind, SpellInfo const* spell)
{
    ++Usage[kind].tried;
    if (spell)
    {
        ++Usage[kind].cast;
        if (kind == USAGE_DISPEL || kind == USAGE_INTERRUPT || kind == USAGE_ATTACK || kind == USAGE_HEAL)
        {
            std::lock_guard<std::mutex> guard(UsageSpellsLock);
            ++UsageSpells[kind][spell->Id];
        }
    }

    time_t const now = time(nullptr);
    time_t last = UsageLastReport.load();
    if (!last)
        UsageLastReport.compare_exchange_strong(last, now);
    else if (now - last >= 10 * MINUTE && UsageLastReport.compare_exchange_strong(last, now))
        ReportUsage(now);

    return spell;
}

// The bot and the living group members near it, the bot first.
std::vector<Player*> NearbyGroup(Player* bot)
{
    std::vector<Player*> members = { bot };
    if (Group* group = bot->GetGroup())
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (member && member != bot && member->IsInWorld() && member->IsAlive() &&
                member->GetMapId() == bot->GetMapId() && bot->IsWithinDistInMap(member, 40.0f))
                members.push_back(member);
        }

    return members;
}

// Whether the unit already carries a buff of this spell category.
//
// Spell.dbc field 1 groups buffs that displace each other: the four Venomancer
// combat venoms are all category 55, the three Primalist boons 2336. Casting a
// second one silently drops the first, so a "cast what is missing" pass never
// settles - one of them is always missing.
bool HasBuffOfCategory(Unit* unit, uint32 category, uint32 exceptSpellId)
{
    if (!category)
        return false;

    for (auto const& [auraId, application] : unit->GetAppliedAuras())
    {
        if (auraId == exceptSpellId || !application->IsPositive())
            continue;

        SpellInfo const* auraInfo = application->GetBase()->GetSpellInfo();
        if (auraInfo && auraInfo->GetCategory() == category)
            return true;
    }

    return false;
}

// Whether the unit carries a harmful aura one of the dispel types in `mask` removes.
bool HasDispellable(Unit* unit, uint32 mask)
{
    for (auto const& [auraId, application] : unit->GetAppliedAuras())
    {
        if (application->IsPositive())
            continue;

        SpellInfo const* auraInfo = application->GetBase()->GetSpellInfo();
        if (auraInfo->Dispel && (mask & (1 << auraInfo->Dispel)))
            return true;
    }

    return false;
}

bool IsInterruptibleCast(Unit* unit)
{
    if (Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL))
        if (spell->getState() == SPELL_STATE_PREPARING &&
            (spell->GetSpellInfo()->InterruptFlags & SPELL_INTERRUPT_FLAG_INTERRUPT))
            return true;

    if (Spell* spell = unit->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
        if (spell->getState() == SPELL_STATE_CASTING &&
            (spell->GetSpellInfo()->ChannelInterruptFlags & CHANNEL_INTERRUPT_FLAG_INTERRUPT))
            return true;

    return false;
}

// The enemy to interrupt: the current target when it casts, else an attacker that does.
Unit* FindCaster(PlayerbotAI* botAI, Player* bot)
{
    Unit* target = botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
    if (target && target->IsAlive() && IsInterruptibleCast(target))
        return target;

    for (ObjectGuid const guid : botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get())
    {
        Unit* attacker = botAI->GetUnit(guid);
        if (attacker && attacker->IsAlive() && bot->IsWithinDistInMap(attacker, 30.0f) && IsInterruptibleCast(attacker))
            return attacker;
    }

    return nullptr;
}

class CoaAttackAction : public Action
{
public:
    // Known to the "threat" strategy, which holds the attack back near the tank's threat.
    ActionThreatType getThreatType() override { return ActionThreatType::Single; }

    CoaAttackAction(PlayerbotAI* botAI) : Action(botAI, "coa attack") {}

    bool Execute(Event /*event*/) override
    {
        Unit* target = AI_VALUE(Unit*, "current target");
        if (!target || !target->IsAlive())
            return false;

        // Playerbots only starts the melee swing for melee bots. A ranged CoA bot caught in melee
        // (a level 1 Ranger: its shot has a minimum range) would then stand there doing nothing and
        // die, so swing the weapon while the target is in reach; spells still go first when they can.
        if (bot->IsWithinMeleeRange(target) && (bot->GetVictim() != target || !bot->HasUnitState(UNIT_STATE_MELEE_ATTACKING)))
            bot->Attack(target, true);

        bool const tank = GetCoaRole(bot) == CoaRole::Tank;

        // A bot that spends its last mana on damage has nothing left when someone drops, and
        // measured on 18/09 that is the usual state: in 99% of the casts turned down for want of
        // power the bot sat below 10% mana. Under its reserve it attacks with what costs nothing
        // (and its weapon), keeping the rest for heals.
        bool const saveMana = SavingManaForHeals(bot);

        std::vector<Usable> const usable = KnownAbilities(bot, [tank](uint16 kind) { return IsAttack(kind, tank); });
        if (usable.empty())
            return false;

        time_t const now = time(nullptr);

        // Rotate through the abilities, starting after the last one that went off, so a
        // bot uses its whole kit instead of spamming the first ability that works.
        for (size_t i = 0; i < usable.size(); ++i)
        {
            size_t const index = (next + i) % usable.size();
            SpellInfo const* info = usable[index].info;

            Strikes& strikes = failures[info->Id];
            if (strikes.benchedUntil > now)
                continue;

            if (saveMana && info->PowerType == POWER_MANA && info->CalcPowerCost(bot, info->GetSchoolMask()) > 0)
                continue;

            if (StrictCheck(bot, info, target) != SPELL_CAST_OK)
                continue;

            if (botAI->CastSpell(info->Id, target))
            {
                strikes.count = 0;
                next = index + 1;
                RecordUsage(USAGE_ATTACK, info);
                return true;
            }

            // Passed the strict check yet refused: something we cannot see from here.
            // Stop insisting for a while rather than burning every tick on it.
            if (++strikes.count >= MaxStrikes)
            {
                strikes.count = 0;
                strikes.benchedUntil = now + BenchSeconds;
            }
        }

        RecordUsage(USAGE_ATTACK, nullptr);
        return false;
    }

    bool isUseful() override
    {
        Unit* target = AI_VALUE(Unit*, "current target");
        return target && target->IsAlive();
    }

private:
    static constexpr uint8 MaxStrikes = 3;
    static constexpr time_t BenchSeconds = 60;

    struct Strikes
    {
        uint8 count = 0;
        time_t benchedUntil = 0;
    };

    size_t next = 0;
    std::unordered_map<uint32, Strikes> failures;
};

// Area attacks on the current target, when several enemies are around.
class CoaAoeAction : public Action
{
public:
    ActionThreatType getThreatType() override { return ActionThreatType::Aoe; }

    CoaAoeAction(PlayerbotAI* botAI) : Action(botAI, "coa aoe") {}

    bool Execute(Event /*event*/) override
    {
        Unit* target = AI_VALUE(Unit*, "current target");
        if (!target || !target->IsAlive())
            return false;

        std::vector<Usable> spells = KnownAbilities(bot, [](uint16 kind)
            { return (kind & KIND_AOE) && (kind & (KIND_DAMAGE | KIND_HOSTILE)); });
        if (SavingManaForHeals(bot))
            DropManaSpells(bot, spells);

        return RecordUsage(USAGE_AOE, CastFirst(botAI, bot, spells, target));
    }

    bool isUseful() override
    {
        Unit* target = AI_VALUE(Unit*, "current target");
        return target && target->IsAlive() && ClassHas(bot, KIND_AOE) &&
               HasReadyAbility(botAI, bot,[](uint16 kind) { return (kind & KIND_AOE) && (kind & (KIND_DAMAGE | KIND_HOSTILE)); });
    }
};

// Heals the most wounded party member (the bot included): a direct heal, an area heal, or a
// heal over time it does not already carry from this bot.
class CoaHealAction : public Action
{
public:
    enum class Mode : uint8
    {
        Direct,
        Group,
        OverTime
    };

    CoaHealAction(PlayerbotAI* botAI, std::string const name, Mode mode) : Action(botAI, name), mode(mode) {}

    bool Execute(Event /*event*/) override
    {
        Unit* target = Target();
        if (!target || !target->IsAlive())
            return false;

        // Drinking or eating: get up to heal. Standing ends the drink, which the healer starts
        // again on its own once nobody needs it any more.
        if (SmartHeal() && !bot->IsStandState())
            bot->SetStandState(UNIT_STAND_STATE_STAND);

        std::vector<Usable> spells;
        switch (mode)
        {
            case Mode::Group:
                spells = KnownAbilities(bot, [](uint16 kind) { return (kind & KIND_GROUP_HEAL) && !(kind & (KIND_CONTROL | KIND_HOSTILE)); });
                break;
            case Mode::OverTime:
            {
                spells = KnownAbilities(bot, [](uint16 kind) { return (kind & KIND_HOT) && !(kind & (KIND_CONTROL | KIND_HOSTILE)); });
                ObjectGuid const caster = bot->GetGUID();
                spells.erase(std::remove_if(spells.begin(), spells.end(),
                    [target, caster](Usable const& spell) { return target->HasAura(spell.info->Id, caster); }),
                    spells.end());
                break;
            }
            default:
                // Single target heals first, direct ones before those over time; area heals last.
                spells = KnownAbilities(bot, [](uint16 kind) { return (kind & KIND_HEAL) && !(kind & (KIND_CONTROL | KIND_HOSTILE)); });
                std::stable_sort(spells.begin(), spells.end(), [](Usable const& a, Usable const& b)
                {
                    auto rank = [](uint16 kind) { return ((kind & KIND_GROUP_HEAL) ? 2 : 0) + ((kind & KIND_HOT) ? 1 : 0); };
                    return rank(a.kind) < rank(b.kind);
                });
                break;
        }

        bool const smart = SmartHeal() && mode == Mode::Direct;
        if (smart)
            OrderByUrgency(bot, target, spells);

        // Someone dropping is healed with the fastest heal even when mana is short; otherwise a
        // bot saving its mana goes for the cheapest.
        if (SavingManaForHeals(bot) && !(smart && target->GetHealthPct() < sPlayerbotAIConfig.criticalHealth))
            CheapestFirst(bot, spells);

        AddPrerequisites(spells);

        UsageKind const usage = mode == Mode::Group ? USAGE_GROUP_HEAL : mode == Mode::OverTime ? USAGE_HOT : USAGE_HEAL;
        if (spells.empty())
            RecordFailure(usage, 0, FAILURE_NOTHING);

        return RecordUsage(usage, CastFirst(botAI, bot, spells, target, usage));
    }

    bool isUseful() override
    {
        Unit* target = Target();
        uint16 const wanted = mode == Mode::Group ? KIND_GROUP_HEAL : mode == Mode::OverTime ? KIND_HOT : KIND_HEAL;
        // A heal over time kept up on the tank in a fight is worth casting before it takes damage.
        bool const hurt = target && (target->GetHealthPct() < (SmartHeal() ? HealLine(bot) : sPlayerbotAIConfig.almostFullHealth) ||
                                     (mode == Mode::OverTime && SmartHeal() && target == GroupTank(bot)));
        return target && target->IsAlive() && hurt &&
               ClassHas(bot, wanted) &&
               HasReadyAbility(botAI, bot,[wanted](uint16 kind) { return (kind & wanted) && !(kind & (KIND_CONTROL | KIND_HOSTILE)); });
    }

private:
    /*
     * A heal refused unless the caster carries some aura: a Witch Doctor's Splash Potion and Potion
     * Toss need "WD Has Ingredient Marker", which its Ingredient spells give. Without it they were set
     * aside 20 s at a time while the Ingredients alone drained the mana. The spells whose name leads
     * with a word of the marker's ("Ingredient: Jungle Shrooms" for "... Ingredient Marker") are put
     * just before such a heal, cheapest first, so the brew is prepared and then thrown.
     */
    void AddPrerequisites(std::vector<Usable>& spells)
    {
        std::vector<Usable> all;
        for (std::size_t i = 0; i < spells.size(); ++i)
        {
            uint32 const needed = spells[i].info->CasterAuraSpell;
            if (!needed || bot->HasAura(needed))
                continue;
            SpellInfo const* marker = sSpellMgr->GetSpellInfo(needed);
            if (!marker || !marker->SpellName[0])
                continue;
            std::string const markerName = marker->SpellName[0];

            if (all.empty())
                all = KnownAbilities(bot, [](uint16) { return true; });

            std::vector<Usable> givers;
            for (Usable const& spell : all)
            {
                std::string const name = spell.info->SpellName[0] ? spell.info->SpellName[0] : "";
                std::size_t const colon = name.find(':');
                if (colon == std::string::npos || colon < 3 || markerName.find(name.substr(0, colon)) == std::string::npos)
                    continue;
                bool present = false;
                for (Usable const& listed : spells)
                    present |= listed.info->Id == spell.info->Id;
                if (!present)
                    givers.push_back(spell);
            }
            if (givers.empty())
                continue;
            CheapestFirst(bot, givers);
            spells.insert(spells.begin() + i, givers.begin(), givers.end());
            i += givers.size();
        }
    }

    // With smart healing, single target heals go where they are most needed (SmartHealTarget), and a
    // heal over time with nobody hurt goes on the tank in a fight; area heals keep the generic choice,
    // whose position is all that matters to them.
    Unit* Target()
    {
        if (!SmartHeal() || mode == Mode::Group)
            return AI_VALUE(Unit*, "party member to heal");

        Unit* target = SmartHealTarget(bot, HealLine(bot), mode == Mode::OverTime);
        // The heal over time kept on the tank before the hits land, while the mana allows it.
        if (!target && mode == Mode::OverTime && bot->IsInCombat() &&
            (bot->getPowerType() != POWER_MANA || bot->GetPowerPct(POWER_MANA) >= 50.0f))
            target = GroupTank(bot);
        return target;
    }

    Mode mode;
};

// Out of a fight, a group member dropping while the healer drinks: a player pulling on their own, a
// straggler. Under 45% health it gets up if it has a quarter of its mana; under 25%, whatever it has.
class CoaGroupMemberDroppingTrigger : public Trigger
{
public:
    CoaGroupMemberDroppingTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa group member dropping") {}

    bool IsActive() override
    {
        if (!SmartHeal() || GetCoaRole(bot) != CoaRole::Heal || !bot->IsAlive() || !bot->GetGroup())
            return false;
        Unit* target = SmartHealTarget(bot, sPlayerbotAIConfig.lowHealth);
        if (!target)
            return false;
        return target->GetHealthPct() < sPlayerbotAIConfig.criticalHealth ||
               bot->getPowerType() != POWER_MANA || bot->GetPowerPct(POWER_MANA) >= 25.0f;
    }
};

// In a fight, a tank carrying none of this healer's heals over time, while it knows one.
class CoaTankNeedsHotTrigger : public Trigger
{
public:
    CoaTankNeedsHotTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa tank needs hot") {}

    bool IsActive() override
    {
        if (!SmartHeal() || !bot->IsInCombat() || !ClassHas(bot, KIND_HOT))
            return false;
        Player* tank = GroupTank(bot);
        if (!tank || bot->GetDistance2d(tank) > sPlayerbotAIConfig.healDistance)
            return false;

        // Before the hits land, not while the tank waits for the next pull at full health: a heal
        // over time costs 15-20% of base mana, renewed on a tank nobody hits it is mana thrown away.
        if (tank->getAttackers().empty() && tank->GetHealthPct() >= 100.0f)
            return false;

        ObjectGuid const caster = bot->GetGUID();
        for (Usable const& spell : KnownAbilities(bot, [](uint16 kind)
                 { return (kind & KIND_HOT) && !(kind & (KIND_CONTROL | KIND_HOSTILE)); }))
            if (tank->HasAura(spell.info->Id, caster))
                return false;
        return true;
    }
};

// A healer too far from the tank, or out of its sight, in a fight.
class CoaFarFromTankTrigger : public Trigger
{
public:
    CoaFarFromTankTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa far from tank") {}

    bool IsActive() override
    {
        if (!SmartHeal() || !bot->IsInCombat())
            return false;
        Player* tank = GroupTank(bot);
        return tank && (bot->GetDistance2d(tank) > StayNearTank + 4.0f || !bot->IsWithinLOSInMap(tank));
    }
};

// Moves back within StayNearTank of the tank, between two casts.
class CoaStayNearTankAction : public MovementAction
{
public:
    CoaStayNearTankAction(PlayerbotAI* botAI) : MovementAction(botAI, "coa stay near tank") {}

    bool Execute(Event /*event*/) override
    {
        Player* tank = GroupTank(bot);
        return tank && MoveNear(tank, StayNearTank - 4.0f);
    }

    bool isUseful() override { return !bot->IsNonMeleeSpellCast(false, true, true); }
};

// A healer running out of mana in a group with a player says so, as a player healer would, so that
// the group waits for it before the next pull. Once per fight.
class CoaLowManaTrigger : public Trigger
{
public:
    CoaLowManaTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa healer low mana") {}

    bool IsActive() override
    {
        bool& said = static_cast<CoaAiObjectContext*>(botAI->GetAiObjectContext())->lowManaSaid;
        if (!bot->IsInCombat())
            said = false;
        if (said || !SmartHeal() || bot->getPowerType() != POWER_MANA || GetCoaRole(bot) != CoaRole::Heal ||
            !bot->IsInCombat() || bot->GetPowerPct(POWER_MANA) >= 20.0f)
            return false;
        Player* master = botAI->GetMaster();
        return master && !GET_PLAYERBOT_AI(master) && bot->GetGroup();
    }
};

class CoaSayLowManaAction : public Action
{
public:
    CoaSayLowManaAction(PlayerbotAI* botAI) : Action(botAI, "coa say low mana") {}

    bool Execute(Event /*event*/) override
    {
        static_cast<CoaAiObjectContext*>(botAI->GetAiObjectContext())->lowManaSaid = true;
        return botAI->SayToParty("Low on mana, give me a moment after this fight.");
    }
};

// Takes the current target back when it attacks someone else.
class CoaTauntAction : public Action
{
public:
    CoaTauntAction(PlayerbotAI* botAI) : Action(botAI, "coa taunt") {}

    bool Execute(Event /*event*/) override
    {
        Unit* target = AI_VALUE(Unit*, "current target");
        if (!target || !target->IsAlive())
            return false;

        SpellInfo const* taunt = CastFirst(botAI, bot,
            KnownAbilities(bot, [](uint16 kind) { return (kind & KIND_TAUNT) != 0; }), target, USAGE_TAUNT);
        if (taunt)
            CoaTelemetryNoteTaunt(bot);
        return RecordUsage(USAGE_TAUNT, taunt);
    }

    bool isUseful() override
    {
        Unit* target = AI_VALUE(Unit*, "current target");
        return target && target->IsAlive() && target->GetVictim() && target->GetVictim() != bot &&
               ClassHas(bot, KIND_TAUNT) && HasReadyAbility(botAI, bot,[](uint16 kind) { return (kind & KIND_TAUNT) != 0; });
    }
};

// When hurt: a defensive cooldown, or failing that a heal on itself.
class CoaDefensiveAction : public Action
{
public:
    CoaDefensiveAction(PlayerbotAI* botAI) : Action(botAI, "coa defensive") {}

    bool Execute(Event /*event*/) override
    {
        SpellInfo const* cast =
            CastFirst(botAI, bot, KnownAbilities(bot, [](uint16 kind) { return (kind & KIND_DEFENSIVE) != 0; }), bot);
        if (!cast)
        {
            std::vector<Usable> heals = KnownAbilities(bot, [](uint16 kind)
                { return (kind & KIND_HEAL) && !(kind & (KIND_GROUP_HEAL | KIND_CONTROL | KIND_HOSTILE)); });
            if (SavingManaForHeals(bot))
                CheapestFirst(bot, heals);

            cast = CastFirst(botAI, bot, heals, bot);
        }

        return RecordUsage(USAGE_DEFENSIVE, cast);
    }

    bool isUseful() override
    {
        return bot->IsAlive() && ClassHas(bot, KIND_DEFENSIVE | KIND_HEAL) && HasReadyAbility(botAI, bot,[](uint16 kind)
            { return (kind & KIND_DEFENSIVE) || ((kind & KIND_HEAL) && !(kind & (KIND_GROUP_HEAL | KIND_CONTROL | KIND_HOSTILE))); });
    }
};

// Removes a harmful magic, curse, disease or poison effect from the bot or a group member.
class CoaDispelAction : public Action
{
public:
    CoaDispelAction(PlayerbotAI* botAI) : Action(botAI, "coa dispel") {}

    bool Execute(Event /*event*/) override
    {
        std::vector<Usable> const spells = KnownAbilities(bot, IsFriendlyDispel);
        if (spells.empty())
            return false;

        for (Player* member : NearbyGroup(bot))
            for (Usable const& spell : spells)
            {
                // Some cleanses only work on the caster.
                if (member != bot && !(spell.kind & KIND_ALLY_CAST))
                    continue;

                if (!HasDispellable(member, spell.dispelMask))
                    continue;

                if (SpellInfo const* cast = CastFirst(botAI, bot, { spell }, member, USAGE_DISPEL))
                    return RecordUsage(USAGE_DISPEL, cast);
            }

        return RecordUsage(USAGE_DISPEL, nullptr);
    }

    bool isUseful() override { return ClassHas(bot, KIND_DISPEL) && HasReadyAbility(botAI, bot,IsFriendlyDispel); }
};

// Kicks the cast of the current target or of an attacker.
class CoaInterruptAction : public Action
{
public:
    CoaInterruptAction(PlayerbotAI* botAI) : Action(botAI, "coa interrupt") {}

    bool Execute(Event /*event*/) override
    {
        Unit* caster = FindCaster(botAI, bot);
        if (!caster)
            return false;

        return RecordUsage(USAGE_INTERRUPT,
            CastFirst(botAI, bot, KnownAbilities(bot, [](uint16 kind) { return (kind & KIND_INTERRUPT) != 0; }), caster));
    }

    bool isUseful() override
    {
        return ClassHas(bot, KIND_INTERRUPT) && HasReadyAbility(botAI, bot,[](uint16 kind) { return (kind & KIND_INTERRUPT) != 0; });
    }
};

// Out of combat: long buffs on the bot and its group.
// Whether taking this form would stop the bot casting its heals (SPELL_ATTR0_NOT_SHAPESHIFTED).
bool FormBlocksHeals(Player* bot, SpellInfo const* form)
{
    uint32 shape = 0;
    for (SpellEffectInfo const& effect : form->Effects)
        if (effect.Effect == SPELL_EFFECT_APPLY_AURA && effect.ApplyAuraName == SPELL_AURA_MOD_SHAPESHIFT)
            shape = uint32(effect.MiscValue);
    if (!shape)
        return false;

    for (Usable const& heal : KnownAbilities(bot, [](uint16 kind)
             { return (kind & (KIND_HEAL | KIND_HOT)) && !(kind & (KIND_CONTROL | KIND_HOSTILE)); }))
        if (heal.info->CheckShapeshift(shape) != SPELL_CAST_OK)
            return true;
    return false;
}

class CoaBuffAction : public Action
{
public:
    CoaBuffAction(PlayerbotAI* botAI) : Action(botAI, "coa buff") {}

    bool Execute(Event /*event*/) override
    {
        if (bot->IsInCombat() || bot->IsMounted() || bot->IsInFlight() || !bot->IsAlive())
            return false;

        std::vector<Usable> const spells = KnownAbilities(bot, [](uint16 kind)
            { return (kind & (KIND_BUFF | KIND_STANCE)) &&
                     !(kind & (KIND_HOSTILE | KIND_DAMAGE | KIND_TAUNT | KIND_HEAL | KIND_CONTROL)); });
        if (spells.empty())
            return false;

        // A stance replaces the one the bot is in, and CoA classes have several of them
        // (five Boons, twenty-two Runic Tattoos): take one only while standing in none, or
        // two of them would take turns for ever.
        bool const inStance = std::any_of(spells.begin(), spells.end(), [this](Usable const& spell)
            { return (spell.kind & KIND_STANCE) && bot->HasAura(spell.info->Id); });

        time_t const now = time(nullptr);
        if (recent.size() > 64)
            for (auto itr = recent.begin(); itr != recent.end();)
                itr = itr->second <= now ? recent.erase(itr) : std::next(itr);

        for (Usable const& spell : spells)
            for (Player* member : NearbyGroup(bot))
            {
                if (member != bot && !(spell.kind & KIND_ALLY_CAST))
                    continue;

                // A stance is the bot's own, and only when it stands in none.
                if ((spell.kind & KIND_STANCE) && (member != bot || inStance))
                    continue;

                // A healer keeps out of a form its heals cannot be cast in.
                if ((spell.kind & KIND_STANCE) && GetCoaRole(bot) == CoaRole::Heal && FormBlocksHeals(bot, spell.info))
                    continue;

                if (member->HasAura(spell.info->Id))
                    continue;

                // One buff per category: several of a displacing group would
                // chase each other forever. See HasBuffOfCategory.
                if (HasBuffOfCategory(member, spell.info->GetCategory(), spell.info->Id))
                    continue;

                // The aura may come from a triggered spell under another id: do not recast
                // it on the same member before it would have run out.
                auto const key = std::make_pair(member->GetGUID(), spell.info->Id);
                auto const found = recent.find(key);
                if (found != recent.end() && found->second > now)
                    continue;

                if (StrictCheck(bot, spell.info, member) == SPELL_CAST_OK && botAI->CastSpell(spell.info->Id, member))
                {
                    recent[key] = now + time_t(spell.info->GetMaxDuration() / IN_MILLISECONDS * 9 / 10);
                    return RecordUsage(USAGE_BUFF, spell.info);
                }
            }

        return false;
    }

    bool isUseful() override { return ClassHas(bot, KIND_BUFF | KIND_STANCE); }

private:
    std::map<std::pair<ObjectGuid, uint32>, time_t> recent;
};

// A group member, the bot included, carries something the bot knows how to dispel.
class CoaDispelTrigger : public Trigger
{
public:
    CoaDispelTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa dispel") {}

    bool IsActive() override
    {
        if (!ClassHas(bot, KIND_DISPEL))
            return false;

        std::vector<Usable> const spells = KnownAbilities(bot, IsFriendlyDispel);
        for (Player* member : NearbyGroup(bot))
            for (Usable const& spell : spells)
                if ((member == bot || (spell.kind & KIND_ALLY_CAST)) && HasDispellable(member, spell.dispelMask))
                    return true;

        return false;
    }
};

// An enemy near the bot is casting something that can be interrupted.
class CoaEnemyCastingTrigger : public Trigger
{
public:
    CoaEnemyCastingTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa enemy casting") {}

    bool IsActive() override
    {
        return ClassHas(bot, KIND_INTERRUPT) && FindCaster(botAI, bot) &&
               HasReadyAbility(botAI, bot,[](uint16 kind) { return (kind & KIND_INTERRUPT) != 0; });
    }
};

/*
 * Damage dealer: use the class abilities every tick, area attacks on packs, interrupts,
 * dispels, and a defensive when hurt. Melee specializations close in; ranged ones keep the
 * "reach spell" distance of CombatStrategy.
 */
/*
 * A damage dealer's threat on `target` as a share of the highest threat a living tank of its group
 * holds on it: 1.0 is level with the tank. A large number while no tank has touched it yet, 0 with
 * no tank. (mod-playerbots' "threat" value is a uint8 percentage: 300% read as 44%.)
 */
float ThreatShare(Player* bot, Unit* target)
{
    Group* group = bot->GetGroup();
    if (!group || !target || target->GetTypeId() != TYPEID_UNIT || !target->IsInCombat())
        return 0.0f;

    bool tank = false;
    float tankThreat = 0.0f;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == bot || !member->IsAlive() || !OnSameInstance(bot, member) || !PlayerbotAI::IsTank(member))
            continue;
        tank = true;
        tankThreat = std::max(tankThreat, target->GetThreatMgr().GetThreat(member));
    }
    if (!tank)
        return 0.0f;
    if (tankThreat <= 0.0f)
        return 100.0f;
    return target->GetThreatMgr().GetThreat(bot) / tankThreat;
}

/*
 * A damage dealer lets the tank open, as players do: on a target no tank has touched yet it waits, at
 * most AiPlayerbot.CoaTankOpenerSeconds, then goes all out - holding aggro is the tank's job. Only
 * with AiPlayerbot.CoaThreatHold set does it also hold back at that percent of the tank's threat (in
 * WoW aggro passes at 110% in melee, 130% at range). mod-playerbots' own rule, stopping at 80% of
 * the tank's threat, held CoA damage dealers to a trickle: CoA tanks make little threat.
 */
class CoaThreatMultiplier : public Multiplier
{
public:
    CoaThreatMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "coa threat") {}

    float GetValue(Action* action) override
    {
        if (!sPlayerbotAIConfig.coaSmartTank || !action || action->getThreatType() == Action::ActionThreatType::None ||
            GetCoaRole(bot) != CoaRole::Dps)
            return 1.0f;
        Unit* target = AI_VALUE(Unit*, "current target");
        if (!target)
            return 1.0f;

        float const share = ThreatShare(bot, target);
        if (share >= 100.0f)  // the tank has not touched it
        {
            // Counted per target from the first time it was seen untouched: a damage dealer turning
            // between the enemies of a pack must not start the wait again at every turn.
            uint32 const now = getMSTime();
            if (openers.size() > 32)
                openers.clear();
            auto const seen = openers.emplace(target->GetGUID(), now).first;
            return getMSTimeDiff(seen->second, now) < sPlayerbotAIConfig.coaTankOpenerSeconds * IN_MILLISECONDS ? 0.0f : 1.0f;
        }

        uint32 const hold = sPlayerbotAIConfig.coaThreatHold;
        return hold && share * 100.0f >= float(hold) ? 0.0f : 1.0f;
    }

private:
    std::unordered_map<ObjectGuid, uint32> openers;  // target -> first seen untouched by a tank
};

class CoaCombatStrategy : public CombatStrategy
{
public:
    void InitMultipliers(std::vector<Multiplier*>& multipliers) override
    {
        CombatStrategy::InitMultipliers(multipliers);
        multipliers.push_back(new CoaThreatMultiplier(botAI));
    }

    CoaCombatStrategy(PlayerbotAI* botAI, bool ranged = false) : CombatStrategy(botAI), ranged(ranged) {}

    std::string const getName() override { return ranged ? "coa ranged" : "coa"; }
    uint32 GetType() const override
    {
        return CombatStrategy::GetType() | STRATEGY_TYPE_DPS | (ranged ? STRATEGY_TYPE_RANGED : 0);
    }

    std::vector<NextAction> getDefaultActions() override
    {
        return { NextAction("coa attack", ACTION_NORMAL) };
    }

    void InitTriggers(std::vector<TriggerNode*>& triggers) override
    {
        // "invalid target" -> "drop target" is what hands the bot back to the non-combat
        // engine once its target dies. Without it the bot stays in combat mode, facing the
        // corpse, and never loots, quests or moves again.
        CombatStrategy::InitTriggers(triggers);

        // Most CoA melee abilities fail with SPELL_FAILED_OUT_OF_RANGE until the bot closes
        // in. Same trigger and priority as MeleeCombatStrategy.
        if (!ranged)
            triggers.push_back(new TriggerNode("enemy out of melee", { NextAction("reach melee", ACTION_HIGH + 1) }));

        triggers.push_back(new TriggerNode("coa enemy casting", { NextAction("coa interrupt", InterruptPriority()) }));
        triggers.push_back(new TriggerNode("coa dispel", { NextAction("coa dispel", DispelPriority()) }));
        triggers.push_back(new TriggerNode("medium aoe", { NextAction("coa aoe", ACTION_HIGH + 2) }));
        triggers.push_back(new TriggerNode("low health", { NextAction("coa defensive", ACTION_HIGH + 8) }));
    }

protected:
    virtual float InterruptPriority() { return ACTION_INTERRUPT; }
    virtual float DispelPriority() { return ACTION_NORMAL + 5; }

    bool ranged;
};

// Tank specializations: taunt back whatever turns on someone else, area threat on two
// enemies, defensives from medium health.
class CoaTankStrategy : public CoaCombatStrategy
{
public:
    CoaTankStrategy(PlayerbotAI* botAI) : CoaCombatStrategy(botAI) {}

    std::string const getName() override { return "coa tank"; }
    uint32 GetType() const override { return STRATEGY_TYPE_COMBAT | STRATEGY_TYPE_TANK | STRATEGY_TYPE_MELEE; }

    void InitTriggers(std::vector<TriggerNode*>& triggers) override
    {
        CoaCombatStrategy::InitTriggers(triggers);
        triggers.push_back(new TriggerNode("lose aggro", { NextAction("coa taunt", ACTION_HIGH + 5) }));
        triggers.push_back(new TriggerNode("light aoe", { NextAction("coa aoe", ACTION_HIGH + 3) }));
        triggers.push_back(new TriggerNode("medium health", { NextAction("coa defensive", ACTION_HIGH + 6) }));
    }
};

/*
 * Healer specializations, from range. By urgency: move in range, critical heal, area heal
 * when several members are hurt, heal the low, then heals over time and dispels, and only
 * then attack. Interrupts wait behind the heals.
 */
class CoaHealStrategy : public CoaCombatStrategy
{
public:
    CoaHealStrategy(PlayerbotAI* botAI) : CoaCombatStrategy(botAI, true) {}

    std::string const getName() override { return "coa heal"; }
    uint32 GetType() const override { return STRATEGY_TYPE_COMBAT | STRATEGY_TYPE_HEAL | STRATEGY_TYPE_RANGED; }

    void InitTriggers(std::vector<TriggerNode*>& triggers) override
    {
        CoaCombatStrategy::InitTriggers(triggers);

        // A wounded member out of healing range or line of sight: close in first, above
        // healing (which would fail the range check) and attacking.
        triggers.push_back(new TriggerNode("party member to heal out of spell range",
                                           { NextAction("reach party member to heal", ACTION_CRITICAL_HEAL + 5) }));
        triggers.push_back(new TriggerNode("party member critical health",
                                           { NextAction("coa heal", ACTION_CRITICAL_HEAL + 4) }));
        triggers.push_back(new TriggerNode("medium aoe heal",
                                           { NextAction("coa group heal", ACTION_CRITICAL_HEAL + 3) }));
        triggers.push_back(new TriggerNode("party member low health",
                                           { NextAction("coa heal", ACTION_CRITICAL_HEAL + 2) }));
        triggers.push_back(new TriggerNode("party member medium health",
                                           { NextAction("coa hot", ACTION_CRITICAL_HEAL + 1),
                                             NextAction("coa heal", ACTION_CRITICAL_HEAL) }));
        triggers.push_back(new TriggerNode("party member almost full health",
                                           { NextAction("coa hot", ACTION_MEDIUM_HEAL) }));

        // Smart healing (AiPlayerbot.CoaSmartHeal; these triggers stay silent without it): a heal
        // over time kept on the tank, back within reach of the tank between casts - above the
        // attacks, below every heal - and a word to the group when the mana runs out.
        triggers.push_back(new TriggerNode("coa tank needs hot", { NextAction("coa hot", ACTION_MEDIUM_HEAL - 1) }));
        triggers.push_back(new TriggerNode("coa far from tank", { NextAction("coa stay near tank", ACTION_MEDIUM_HEAL - 2) }));
        triggers.push_back(new TriggerNode("coa healer low mana", { NextAction("coa say low mana", ACTION_MEDIUM_HEAL + 8) }));
    }

protected:
    float InterruptPriority() override { return ACTION_MEDIUM_HEAL + 7; }
    float DispelPriority() override { return ACTION_MEDIUM_HEAL + 6; }
};

/*
 * Auto pull ("nc +coa auto pull" in the group chat, "nc -coa auto pull" to stop): between fights the
 * tank pulls the next pack by itself, the way a player tank does once the group is ready - nobody
 * dead, in a fight, eating or drinking, everyone at 70% health or more, the healers at 70% mana or
 * more. It pulls the nearest hostile creature it can see within 30 yards, never one more than 40
 * yards from the player or on another floor, says what it pulls, and leaves the route to the
 * player: it does not know the dungeon, it takes what is in front of the group. In dungeons only.
 */
constexpr float AutoPullRange = 30.0f;
constexpr float AutoPullLeash = 40.0f;
constexpr float AutoPullFloor = 6.0f;
constexpr time_t AutoPullPause = 6;  // seconds between two pulls, while the first one gets there

bool GroupReadyToPull(Player* bot)
{
    Group* group = bot->GetGroup();
    if (!group)
        return false;

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !OnSameInstance(bot, member))
            continue;
        if (!member->IsAlive() || member->IsInCombat() || !member->IsStandState() || member->GetHealthPct() < 70.0f)
            return false;
        if (PlayerbotAI::IsHeal(member) && member->getPowerType() == POWER_MANA &&
            member->GetPowerPct(POWER_MANA) < 70.0f)
            return false;
    }
    return true;
}

Unit* NextPull(PlayerbotAI* botAI, Player* bot, Player* master)
{
    Unit* best = nullptr;
    float bestDistance = AutoPullRange;
    for (ObjectGuid const& guid : botAI->GetAiObjectContext()->GetValue<GuidVector>("possible targets")->Get())
    {
        Creature* creature = botAI->GetUnit(guid) ? botAI->GetUnit(guid)->ToCreature() : nullptr;
        if (!creature || !creature->IsAlive() || creature->IsInCombat() || creature->IsCritter() ||
            creature->IsCivilian() || creature->IsTotem() || creature->IsPet() || !creature->IsHostileTo(bot))
            continue;

        float const distance = bot->GetDistance(creature);
        if (distance > bestDistance || master->GetDistance(creature) > AutoPullLeash ||
            std::fabs(creature->GetPositionZ() - bot->GetPositionZ()) > AutoPullFloor ||
            !bot->IsWithinLOSInMap(creature))
            continue;

        best = creature;
        bestDistance = distance;
    }
    return best;
}

class CoaReadyToPullTrigger : public Trigger
{
public:
    CoaReadyToPullTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa ready to pull") {}

    bool IsActive() override
    {
        // Dungeons only: in the open world the player is questing or travelling, not asking for pulls.
        if (!sPlayerbotAIConfig.coaSmartTank || !PlayerbotAI::IsTank(bot) || bot->IsInCombat() || !bot->IsAlive() ||
            !bot->GetMap()->IsDungeon())
            return false;
        Player* master = botAI->GetMaster();
        if (!master || GET_PLAYERBOT_AI(master) || !OnSameInstance(bot, master))
            return false;
        time_t const last = static_cast<CoaAiObjectContext*>(botAI->GetAiObjectContext())->lastAutoPull;
        return time(nullptr) - last >= AutoPullPause && GroupReadyToPull(bot) && NextPull(botAI, bot, master);
    }
};

class CoaAutoPullAction : public AttackAction
{
public:
    CoaAutoPullAction(PlayerbotAI* botAI) : AttackAction(botAI, "coa auto pull") {}

    bool Execute(Event /*event*/) override
    {
        Player* master = botAI->GetMaster();
        Unit* target = master ? NextPull(botAI, bot, master) : nullptr;
        if (!target)
            return false;

        static_cast<CoaAiObjectContext*>(botAI->GetAiObjectContext())->lastAutoPull = time(nullptr);
        botAI->SayToParty("Pulling " + target->GetName() + ".");
        return Attack(target);
    }
};

class CoaAutoPullStrategy : public Strategy
{
public:
    CoaAutoPullStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}

    std::string const getName() override { return "coa auto pull"; }
    uint32 GetType() const override { return STRATEGY_TYPE_NONCOMBAT; }

    void InitTriggers(std::vector<TriggerNode*>& triggers) override
    {
        triggers.push_back(new TriggerNode("coa ready to pull", { NextAction("coa auto pull", ACTION_HIGH) }));
    }
};

/*
 * Out of a fight, with nobody of the group fighting, the dead lying where they fell (not released) are
 * brought back by the first bot that has a resurrection: the player first, then the bots of the group,
 * then other players of its faction close by in a dungeon. It walks within 25 yards and in sight, then
 * casts and says so. A tank that died at the end of a pull no longer waits for the player to run back.
 */
constexpr float ResurrectReach = 25.0f;

bool GroupFighting(Player* bot)
{
    Group* group = bot->GetGroup();
    if (!group)
        return false;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        if (Player* member = ref->GetSource())
            if (OnSameInstance(bot, member) && member->IsAlive() && member->IsInCombat())
                return true;
    return false;
}

// Whom to bring back first: a real player of the group, then a bot of the group, then - inside a
// dungeon, where the list of players is short - any other player of the bot's faction lying within
// 30 yards. Nearest first within each. Only the dead who have not released can be raised.
constexpr float ResurrectStrangersWithin = 30.0f;

Player* DeadGroupMember(Player* bot)
{
    Group* group = bot->GetGroup();
    if (!group)
        return nullptr;

    Player* best = nullptr;
    int bestRank = 3;
    float bestDistance = 0.0f;
    auto consider = [&](Player* dead, int rank)
    {
        if (!dead || dead == bot || dead->IsAlive() || dead->HasPlayerFlag(PLAYER_FLAGS_GHOST) || !OnSameInstance(bot, dead))
            return;
        float const distance = dead->GetDistance(bot);
        if (distance > sPlayerbotAIConfig.sightDistance)
            return;
        if (!best || rank < bestRank || (rank == bestRank && distance < bestDistance))
        {
            best = dead;
            bestRank = rank;
            bestDistance = distance;
        }
    };

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        if (Player* member = ref->GetSource())
            consider(member, GET_PLAYERBOT_AI(member) ? 1 : 0);

    if (!best && bot->GetMap()->IsDungeon())
        for (auto const& ref : bot->GetMap()->GetPlayers())
        {
            Player* other = ref.GetSource();
            if (other && !other->IsInSameGroupWith(bot) && other->GetTeamId() == bot->GetTeamId() &&
                other->GetDistance(bot) <= ResurrectStrangersWithin)
                consider(other, 2);
        }

    return best;
}

class CoaGroupMemberDeadTrigger : public Trigger
{
public:
    CoaGroupMemberDeadTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa group member dead") {}

    bool IsActive() override
    {
        return bot->IsAlive() && !bot->IsInCombat() && ClassHas(bot, KIND_RESURRECT) && DeadGroupMember(bot) &&
               !GroupFighting(bot) &&
               HasReadyAbility(botAI, bot, [](uint16 kind) { return (kind & KIND_RESURRECT) != 0; });
    }
};

class CoaResurrectAction : public MovementAction
{
public:
    CoaResurrectAction(PlayerbotAI* botAI) : MovementAction(botAI, "coa resurrect") {}

    bool Execute(Event /*event*/) override
    {
        Player* dead = DeadGroupMember(bot);
        if (!dead)
            return false;

        if (bot->GetDistance(dead) > ResurrectReach || !bot->IsWithinLOSInMap(dead))
            return MoveNear(dead, ResurrectReach - 5.0f);

        if (!bot->IsStandState())
            bot->SetStandState(UNIT_STAND_STATE_STAND);

        SpellInfo const* cast = CastFirst(botAI, bot,
            KnownAbilities(bot, [](uint16 kind) { return (kind & KIND_RESURRECT) != 0; }), dead);
        if (cast)
            botAI->SayToParty("Resurrecting " + dead->GetName() + ".");
        return cast != nullptr;
    }

    bool isUseful() override { return !bot->IsNonMeleeSpellCast(false, true, true); }
};

/*
 * A bot of a real player's group that lost them: more than 35 yards away for 6 seconds, out of a
 * fight, on the same map instance, while the player is not fighting either. It joins them, the way
 * a player would be summoned. mod-playerbots' own "move stuck" never runs for bots of a real player,
 * so one caught on a ramp or behind brambles stayed there until told to follow or teleported.
 */
constexpr float CatchUpDistance = 35.0f;
constexpr time_t CatchUpAfter = 6;

class CoaLostThePlayerTrigger : public Trigger
{
public:
    CoaLostThePlayerTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa lost the player") {}

    bool IsActive() override
    {
        time_t& farSince = static_cast<CoaAiObjectContext*>(botAI->GetAiObjectContext())->farFromPlayerSince;
        Player* master = botAI->GetMaster();
        bool const lost = master && !GET_PLAYERBOT_AI(master) && bot->IsAlive() && master->IsAlive() &&
                         !bot->IsInCombat() && !master->IsInCombat() && !master->IsInFlight() &&
                         !master->IsBeingTeleported() && OnSameInstance(bot, master) &&
                         bot->GetGroup() && bot->GetDistance(master) > CatchUpDistance;
        if (!lost)
        {
            farSince = 0;
            return false;
        }
        if (!farSince)
            farSince = time(nullptr);
        return time(nullptr) - farSince >= CatchUpAfter;
    }
};

class CoaCatchUpAction : public Action
{
public:
    CoaCatchUpAction(PlayerbotAI* botAI) : Action(botAI, "coa catch up") {}

    bool Execute(Event /*event*/) override
    {
        Player* master = botAI->GetMaster();
        if (!master)
            return false;

        static_cast<CoaAiObjectContext*>(botAI->GetAiObjectContext())->farFromPlayerSince = 0;
        bot->StopMoving();
        bot->NearTeleportTo(master->GetPositionX(), master->GetPositionY(), master->GetPositionZ(),
                            master->GetOrientation());
        return true;
    }
};

// Out of combat: keep the long buffs up on the bot and its group.
class CoaBuffStrategy : public Strategy
{
public:
    CoaBuffStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}

    std::string const getName() override { return "coa buff"; }
    uint32 GetType() const override { return STRATEGY_TYPE_NONCOMBAT; }

    void InitTriggers(std::vector<TriggerNode*>& triggers) override
    {
        triggers.push_back(new TriggerNode("often", { NextAction("coa buff", ACTION_NORMAL + 5) }));
        // A healer heals a group member in danger even outside a fight, before drinking or buffing.
        triggers.push_back(new TriggerNode("coa group member dropping", { NextAction("coa heal", ACTION_CRITICAL_HEAL) }));
        // Lost the player on the way: join them.
        triggers.push_back(new TriggerNode("coa lost the player", { NextAction("coa catch up", ACTION_HIGH + 5) }));
        // A dead group member is brought back once the group is out of the fight, before anything else.
        triggers.push_back(new TriggerNode("coa group member dead", { NextAction("coa resurrect", ACTION_CRITICAL_HEAL + 5) }));
    }
};

class CoaStrategyFactoryInternal : public NamedObjectContext<Strategy>
{
public:
    CoaStrategyFactoryInternal() : NamedObjectContext<Strategy>(false, true)
    {
        creators["coa"] = &CoaStrategyFactoryInternal::coa;
        creators["coa ranged"] = &CoaStrategyFactoryInternal::coa_ranged;
        creators["coa tank"] = &CoaStrategyFactoryInternal::coa_tank;
        creators["coa heal"] = &CoaStrategyFactoryInternal::coa_heal;
        creators["coa buff"] = &CoaStrategyFactoryInternal::coa_buff;
        creators["coa auto pull"] = &CoaStrategyFactoryInternal::coa_auto_pull;
    }

private:
    static Strategy* coa(PlayerbotAI* botAI) { return new CoaCombatStrategy(botAI); }
    static Strategy* coa_auto_pull(PlayerbotAI* botAI) { return new CoaAutoPullStrategy(botAI); }
    static Strategy* coa_ranged(PlayerbotAI* botAI) { return new CoaCombatStrategy(botAI, true); }
    static Strategy* coa_tank(PlayerbotAI* botAI) { return new CoaTankStrategy(botAI); }
    static Strategy* coa_heal(PlayerbotAI* botAI) { return new CoaHealStrategy(botAI); }
    static Strategy* coa_buff(PlayerbotAI* botAI) { return new CoaBuffStrategy(botAI); }
};

class CoaActionFactoryInternal : public NamedObjectContext<Action>
{
public:
    CoaActionFactoryInternal()
    {
        creators["coa attack"] = &CoaActionFactoryInternal::coa_attack;
        creators["coa aoe"] = &CoaActionFactoryInternal::coa_aoe;
        creators["coa heal"] = &CoaActionFactoryInternal::coa_heal;
        creators["coa group heal"] = &CoaActionFactoryInternal::coa_group_heal;
        creators["coa hot"] = &CoaActionFactoryInternal::coa_hot;
        creators["coa taunt"] = &CoaActionFactoryInternal::coa_taunt;
        creators["coa defensive"] = &CoaActionFactoryInternal::coa_defensive;
        creators["coa dispel"] = &CoaActionFactoryInternal::coa_dispel;
        creators["coa interrupt"] = &CoaActionFactoryInternal::coa_interrupt;
        creators["coa buff"] = &CoaActionFactoryInternal::coa_buff;
        creators["coa stay near tank"] = &CoaActionFactoryInternal::coa_stay_near_tank;
        creators["coa say low mana"] = &CoaActionFactoryInternal::coa_say_low_mana;
        creators["coa auto pull"] = &CoaActionFactoryInternal::coa_auto_pull;
        creators["coa resurrect"] = &CoaActionFactoryInternal::coa_resurrect;
        creators["coa catch up"] = &CoaActionFactoryInternal::coa_catch_up;
    }

private:
    static Action* coa_attack(PlayerbotAI* botAI) { return new CoaAttackAction(botAI); }
    static Action* coa_aoe(PlayerbotAI* botAI) { return new CoaAoeAction(botAI); }
    static Action* coa_heal(PlayerbotAI* botAI)
    {
        return new CoaHealAction(botAI, "coa heal", CoaHealAction::Mode::Direct);
    }
    static Action* coa_group_heal(PlayerbotAI* botAI)
    {
        return new CoaHealAction(botAI, "coa group heal", CoaHealAction::Mode::Group);
    }
    static Action* coa_hot(PlayerbotAI* botAI) { return new CoaHealAction(botAI, "coa hot", CoaHealAction::Mode::OverTime); }
    static Action* coa_taunt(PlayerbotAI* botAI) { return new CoaTauntAction(botAI); }
    static Action* coa_defensive(PlayerbotAI* botAI) { return new CoaDefensiveAction(botAI); }
    static Action* coa_dispel(PlayerbotAI* botAI) { return new CoaDispelAction(botAI); }
    static Action* coa_interrupt(PlayerbotAI* botAI) { return new CoaInterruptAction(botAI); }
    static Action* coa_buff(PlayerbotAI* botAI) { return new CoaBuffAction(botAI); }
    static Action* coa_stay_near_tank(PlayerbotAI* botAI) { return new CoaStayNearTankAction(botAI); }
    static Action* coa_say_low_mana(PlayerbotAI* botAI) { return new CoaSayLowManaAction(botAI); }
    static Action* coa_auto_pull(PlayerbotAI* botAI) { return new CoaAutoPullAction(botAI); }
    static Action* coa_resurrect(PlayerbotAI* botAI) { return new CoaResurrectAction(botAI); }
    static Action* coa_catch_up(PlayerbotAI* botAI) { return new CoaCatchUpAction(botAI); }
};

class CoaTriggerFactoryInternal : public NamedObjectContext<Trigger>
{
public:
    CoaTriggerFactoryInternal()
    {
        creators["coa dispel"] = &CoaTriggerFactoryInternal::coa_dispel;
        creators["coa enemy casting"] = &CoaTriggerFactoryInternal::coa_enemy_casting;
        creators["coa tank needs hot"] = &CoaTriggerFactoryInternal::coa_tank_needs_hot;
        creators["coa group member dropping"] = &CoaTriggerFactoryInternal::coa_group_member_dropping;
        creators["coa ready to pull"] = &CoaTriggerFactoryInternal::coa_ready_to_pull;
        creators["coa group member dead"] = &CoaTriggerFactoryInternal::coa_group_member_dead;
        creators["coa lost the player"] = &CoaTriggerFactoryInternal::coa_lost_the_player;
        creators["coa far from tank"] = &CoaTriggerFactoryInternal::coa_far_from_tank;
        creators["coa healer low mana"] = &CoaTriggerFactoryInternal::coa_healer_low_mana;
    }

private:
    static Trigger* coa_dispel(PlayerbotAI* botAI) { return new CoaDispelTrigger(botAI); }
    static Trigger* coa_enemy_casting(PlayerbotAI* botAI) { return new CoaEnemyCastingTrigger(botAI); }
    static Trigger* coa_tank_needs_hot(PlayerbotAI* botAI) { return new CoaTankNeedsHotTrigger(botAI); }
    static Trigger* coa_group_member_dropping(PlayerbotAI* botAI) { return new CoaGroupMemberDroppingTrigger(botAI); }
    static Trigger* coa_ready_to_pull(PlayerbotAI* botAI) { return new CoaReadyToPullTrigger(botAI); }
    static Trigger* coa_group_member_dead(PlayerbotAI* botAI) { return new CoaGroupMemberDeadTrigger(botAI); }
    static Trigger* coa_lost_the_player(PlayerbotAI* botAI) { return new CoaLostThePlayerTrigger(botAI); }
    static Trigger* coa_far_from_tank(PlayerbotAI* botAI) { return new CoaFarFromTankTrigger(botAI); }
    static Trigger* coa_healer_low_mana(PlayerbotAI* botAI) { return new CoaLowManaTrigger(botAI); }
};

}  // namespace

// Whether a spell heals or shields an ally, by the classifier (triggered spells included) or by an
// absorb it puts on its target. Gaze of C'Thun hits enemies or heals allies: not a heal to the
// classifier, yet the Cultist healer's main heal (its rotation's "can cast" line).
bool HealsOrShieldsDirectly(SpellInfo const* info, uint8 depth = 0)
{
    for (SpellEffectInfo const& effect : info->Effects)
    {
        if (effect.Effect == SPELL_EFFECT_HEAL || effect.Effect == SPELL_EFFECT_HEAL_PCT ||
            ((effect.Effect == SPELL_EFFECT_APPLY_AURA || effect.Effect == SPELL_EFFECT_APPLY_AREA_AURA_PARTY ||
              effect.Effect == SPELL_EFFECT_APPLY_AREA_AURA_RAID) &&
             (effect.ApplyAuraName == SPELL_AURA_PERIODIC_HEAL || effect.ApplyAuraName == SPELL_AURA_SCHOOL_ABSORB)))
            return true;
        // The heal of many CoA spells is in the spell they trigger (Gaze of C'Thun).
        if (depth < 2 && effect.TriggerSpell && effect.TriggerSpell != info->Id)
            if (SpellInfo const* triggered = sSpellMgr->GetSpellInfo(effect.TriggerSpell))
                if (HealsOrShieldsDirectly(triggered, depth + 1))
                    return true;
    }
    return false;
}

bool HealsOrShields(Player* bot, SpellInfo const* info)
{
    if (HealsOrShieldsDirectly(info))
        return true;

    auto const& all = ClassAbilities();
    auto const found = all.find(bot->getClass());
    if (found == all.end())
        return false;
    uint32 const first = info->GetFirstRankSpell()->Id;
    for (CoaAbility const& ability : found->second.abilities)
        if ((ability.spellId == info->Id || ability.firstSpellId == first) && (ability.kind & (KIND_HEAL | KIND_HOT)))
            return true;
    return false;
}

bool CoaHealerSavesManaFrom(Player* bot, SpellInfo const* info)
{
    // What heals or shields stays: saving mana for heals must not take the heals away (a Cultist
    // healer lost Gaze of C'Thun, its main heal: 18 healing a second, the tank 44 s under half).
    return info && GetCoaRole(bot) == CoaRole::Heal && info->PowerType == POWER_MANA &&
           info->CalcPowerCost(bot, info->GetSchoolMask()) > 0 && SavingManaForHeals(bot) && !HealsOrShields(bot, info);
}

bool CoaHealerAvoidsForm(Player* bot, SpellInfo const* info)
{
    return info && GetCoaRole(bot) == CoaRole::Heal && FormBlocksHeals(bot, info);
}

/*
 * Spells of which only one may be active at a time: casting one removes the others. The rule lives in
 * mod-ascension-compat's aura scripts (AscensionPyromancerAuras.cpp, AscensionCultistAuras.cpp) and
 * not in the spell data, so the families are mirrored here. Keep them in step with those files.
 */
std::vector<std::vector<uint32>> const ExclusiveFamilies =
{
    { 504707, 504720, 680387, 681314 },                                               // Pyromancer skins
    { 1119751, 1119754, 1119755, 1119756, 1119757, 1119758, 1119901, 1119944, 1119953 }, // Ascension auras
    { 803035, 803037, 803082, 803339 },                                               // Cultist
    { 561386, 561387, 561389, 561390, 561391, 561392, 572637, 572791, 572819, 572905, 573067 }, // Cultist
};

bool CoaHoldsExclusiveSibling(Player* bot, SpellInfo const* info)
{
    if (!info)
        return false;

    uint32 const id = info->GetFirstRankSpell()->Id;
    for (std::vector<uint32> const& family : ExclusiveFamilies)
    {
        if (std::find(family.begin(), family.end(), id) == family.end() &&
            std::find(family.begin(), family.end(), info->Id) == family.end())
            continue;
        for (uint32 other : family)
            if (other != id && other != info->Id && bot->HasAura(other))
                return true;
    }
    return false;
}

std::string CoaHealKit(Player* bot)
{
    std::string kit;
    for (Usable const& spell : KnownAbilities(bot, [](uint16 kind)
             { return (kind & (KIND_HEAL | KIND_HOT)) && !(kind & (KIND_CONTROL | KIND_HOSTILE)); }))
    {
        if (!kit.empty())
            kit += ", ";
        kit += spell.info->SpellName[0];
        kit += " (" + std::to_string(spell.info->Id) + ")";
    }
    return kit;
}

SharedNamedObjectContextList<Strategy> CoaAiObjectContext::sharedStrategyContexts;
SharedNamedObjectContextList<Action> CoaAiObjectContext::sharedActionContexts;
SharedNamedObjectContextList<Trigger> CoaAiObjectContext::sharedTriggerContexts;
SharedNamedObjectContextList<UntypedValue> CoaAiObjectContext::sharedValueContexts;

CoaAiObjectContext::CoaAiObjectContext(PlayerbotAI* botAI)
    : AiObjectContext(botAI, sharedStrategyContexts, sharedActionContexts,
                      sharedTriggerContexts, sharedValueContexts)
{
}

void CoaAiObjectContext::BuildSharedContexts()
{
    BuildSharedStrategyContexts(sharedStrategyContexts);
    BuildSharedActionContexts(sharedActionContexts);
    BuildSharedTriggerContexts(sharedTriggerContexts);
    BuildSharedValueContexts(sharedValueContexts);
}

void CoaAiObjectContext::BuildSharedStrategyContexts(SharedNamedObjectContextList<Strategy>& strategyContexts)
{
    AiObjectContext::BuildSharedStrategyContexts(strategyContexts);
    strategyContexts.Add(new CoaStrategyFactoryInternal());
}

void CoaAiObjectContext::BuildSharedActionContexts(SharedNamedObjectContextList<Action>& actionContexts)
{
    AiObjectContext::BuildSharedActionContexts(actionContexts);
    actionContexts.Add(new CoaActionFactoryInternal());
}

void CoaAiObjectContext::BuildSharedTriggerContexts(SharedNamedObjectContextList<Trigger>& triggerContexts)
{
    AiObjectContext::BuildSharedTriggerContexts(triggerContexts);
    triggerContexts.Add(new CoaTriggerFactoryInternal());
}

void CoaAiObjectContext::BuildSharedValueContexts(SharedNamedObjectContextList<UntypedValue>& valueContexts)
{
    AiObjectContext::BuildSharedValueContexts(valueContexts);
}
