/*
 * CoA: implementations of the generic, parameterised triggers.
 * The logic is deliberately identical to the originals in GenericTriggers.cpp -
 * the only difference is that the spell name comes from the qualifier instead
 * of from the const field AiNamedObject::name.
 */
#include "CoaGenericContext.h"

#include "AscensionSpecialization.h"

#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "Helpers.h"
#include "ObjectMgr.h"
#include "Playerbots.h"
#include "PlayerbotAIConfig.h"
#include "CoaSpecialization.h"
#include "GenericBuffUtils.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"
#include <map>
#include <cctype>
#include <cstdlib>
#include <algorithm>
#include <limits>

// How far an enemy cleave, cone or spin around the caster reaches; 0 when the spell reaches its target.
static float CoaCasterCentredReach(Player* bot, SpellInfo const* info)
{
    float reach = 0.0f;
    for (SpellEffectInfo const& effect : info->Effects)
    {
        if (!effect.IsEffect())
            continue;
        uint32 const a = effect.TargetA.GetTarget();
        uint32 const b = effect.TargetB.GetTarget();
        bool const cone = a == TARGET_UNIT_CONE_ENEMY_24 || a == TARGET_UNIT_CONE_ENEMY_54 || a == TARGET_UNIT_CONE_ENEMY_104;
        bool const around = a == TARGET_SRC_CASTER && b == TARGET_UNIT_SRC_AREA_ENEMY;
        if (!cone && !around)
            return 0.0f;
        reach = std::max(reach, effect.CalcRadius(bot));
    }
    return reach;
}

// Whether the bot holds the power the spell costs (CanCastSpell probes ignoring it).
static bool CoaHasPowerFor(Player* bot, SpellInfo const* info)
{
    if (info->PowerType >= MAX_POWERS)
        return true;
    int32 const cost = info->CalcPowerCost(bot, info->GetSchoolMask());
    return cost <= 0 || int32(bot->GetPower(Powers(info->PowerType))) >= cost;
}

// In a dungeon a damage dealer or healer does not open on an idle creature while its group has a living tank: the
// tank pulls. Houndmaster's Call and Unleash the Hounds sent the hounds through 60 idle mobs, Blood Steward included.
static bool CoaPullsAheadOfTank(Player* bot, Unit* target)
{
    if (!target || !target->IsCreature() || target->IsInCombat() || !bot->GetMap()->IsDungeon() ||
        PlayerbotAI::IsTank(bot) || !bot->IsHostileTo(target))
        return false;
    Group* group = bot->GetGroup();
    if (!group)
        return false;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (member && member != bot && member->IsAlive() && member->GetMapId() == bot->GetMapId() &&
            PlayerbotAI::IsTank(member))
            return true;
    }
    return false;
}

// A spell for the bot itself or a friend: no effect aims at an enemy, and the spell is helpful or lands only on the
// caster or its friends. A rotation line "cast::" aims at the current target: Mana Module, Auto Resuscitation Device,
// Essence of Malygos and Void Shield were aimed at the enemy and never cast (ladiabale, 16/10).
static bool CoaSpellForSelfOrFriend(SpellInfo const* info)
{
    bool friendlyOnly = true;
    bool any = false;
    for (SpellEffectInfo const& effect : info->Effects)
    {
        if (!effect.IsEffect())
            continue;
        any = true;
        for (uint32 const target : { effect.TargetA.GetTarget(), effect.TargetB.GetTarget() })
        {
            switch (target)
            {
                case 0:
                case TARGET_UNIT_CASTER:
                case TARGET_SRC_CASTER:
                case TARGET_DEST_CASTER:
                case TARGET_UNIT_TARGET_ALLY:
                case TARGET_UNIT_TARGET_PARTY:
                case TARGET_UNIT_TARGET_RAID:
                case TARGET_UNIT_CASTER_AREA_PARTY:
                case TARGET_UNIT_CASTER_AREA_RAID:
                case TARGET_UNIT_SRC_AREA_ALLY:
                case TARGET_UNIT_SRC_AREA_PARTY:
                case TARGET_UNIT_PET:
                case TARGET_UNIT_MASTER:
                    break;
                case TARGET_UNIT_TARGET_ENEMY:
                case TARGET_DEST_TARGET_ENEMY:
                case TARGET_UNIT_SRC_AREA_ENEMY:
                case TARGET_UNIT_DEST_AREA_ENEMY:
                case TARGET_UNIT_CONE_ENEMY_24:
                case TARGET_UNIT_CONE_ENEMY_54:
                case TARGET_UNIT_CONE_ENEMY_104:
                // Friend or foe (a Holy Shock): the line's target decides.
                case TARGET_UNIT_TARGET_ANY:
                case TARGET_DEST_TARGET_ANY:
                    return false;
                default:
                    friendlyOnly = false;
                    break;
            }
        }
    }
    return any && (friendlyOnly || info->IsPositive());
}

// The bot's own spell for "cast::" and "cast melee::" lines whose spell is not for an enemy.
static Unit* CoaSelfTargetFor(PlayerbotAI* botAI, Player* bot, std::string const& spell)
{
    uint32 const id = botAI->GetAiObjectContext()->GetValue<uint32>("spell id", spell)->Get();
    SpellInfo const* info = id ? sSpellMgr->GetSpellInfo(id) : nullptr;
    return info && CoaSpellForSelfOrFriend(info) ? bot : nullptr;
}

Unit* CoaCastAction::GetTarget()
{
    if (Unit* self = CoaSelfTargetFor(botAI, bot, spell))
        return self;
    return CastSpellAction::GetTarget();
}

Unit* CoaCastMeleeAction::GetTarget()
{
    if (Unit* self = CoaSelfTargetFor(botAI, bot, spell))
        return self;
    return CastMeleeSpellAction::GetTarget();
}

bool CoaCastAction::isUseful()
{
    if (!CastSpellAction::isUseful())
        return false;
    if (CoaPullsAheadOfTank(bot, GetTarget()))
        return false;
    uint32 const id = AI_VALUE2(uint32, "spell id", spell);
    SpellInfo const* info = id ? sSpellMgr->GetSpellInfo(id) : nullptr;
    if (!info)
        return true;
    if (!CoaHasPowerFor(bot, info))
        return false;
    // Celestial Cleave cast 20 yd from its target hit nothing and logged OK
    Unit* target = GetTarget();
    if (target && target != bot)
        if (float const reach = CoaCasterCentredReach(bot, info))
            return bot->IsWithinDistInMap(target, reach + target->GetCombatReach());
    return true;
}

bool CoaBuffPartyAction::isUseful()
{
    return AI_VALUE2(uint32, "spell id", spell) && BuffOnPartyAction::isUseful();
}

bool CoaCastMeleeAction::isUseful()
{
    if (!CastMeleeSpellAction::isUseful() || CoaPullsAheadOfTank(bot, GetTarget()))
        return false;
    uint32 const id = AI_VALUE2(uint32, "spell id", spell);
    SpellInfo const* info = id ? sSpellMgr->GetSpellInfo(id) : nullptr;
    return !info || CoaHasPowerFor(bot, info);
}

// The dispel type of a cure spell, taken from the spell itself.
//
// SPELL_EFFECT_DISPEL carries the type as its MiscValue - 1 magic, 2 curse,
// 3 disease, 4 poison. Dispel Magic (527) is the calibration case: effect 38,
// MiscValue 1.
//
// The name is resolved against the bot's own spellbook, so a spell the bot has
// not learned yields 0, and so does a spell that dispels nothing.
uint32 CoaDispelTypeOf(PlayerbotAI* botAI, std::string const& spellName)
{
    if (spellName.empty())
        return 0;

    uint32 const spellId =
        botAI->GetAiObjectContext()->GetValue<uint32>("spell id", spellName)->Get();
    if (!spellId)
        return 0;

    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
    if (!info)
        return 0;

    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        if (info->Effects[i].Effect == SPELL_EFFECT_DISPEL)
            return uint32(info->Effects[i].MiscValue);

    return 0;
}

bool CoaHasAuraTrigger::IsActive()
{
    if (qualifier.empty())
        return false;
    // as in HasAuraTrigger::IsActive
    return botAI->HasAura(qualifier, GetTarget(), false, false, -1, true);
}

bool CoaTargetHasAuraTrigger::IsActive()
{
    if (qualifier.empty())
        return false;
    Unit* target = GetTarget();
    return target && botAI->HasAura(qualifier, target, false, false, -1, true);
}

bool CoaHasNoAuraTrigger::IsActive()
{
    if (qualifier.empty())
        return false;
    // as in HasNoAuraTrigger::IsActive
    return !botAI->HasAura(qualifier, GetTarget());
}

bool CoaAoeHasAuraTrigger::IsActive()
{
    return !qualifier.empty() && MediumAoeTrigger::IsActive() &&
           botAI->HasAura(qualifier, bot, false, false, -1, true);
}

std::string CoaAuraStacksTrigger::SpellPart() const
{
    size_t comma = qualifier.rfind(',');
    return comma == std::string::npos ? qualifier : qualifier.substr(0, comma);
}

int CoaAuraStacksTrigger::StackPart() const
{
    size_t comma = qualifier.rfind(',');
    if (comma == std::string::npos)
        return 1;
    return atoi(qualifier.c_str() + comma + 1);
}

bool CoaAuraStacksTrigger::IsActive()
{
    std::string const spellName = SpellPart();
    if (spellName.empty())
        return false;
    // as in HasAuraStackTrigger::IsActive
    // checkDuration MUST be false.
    //
    // With checkDuration=true PlayerbotAI::GetAura skips every aura whose
    // GetDuration() is -1, that is every PERMANENT one. Class resources are
    // exactly that: Felfury, Spirit, Insanity and the rest stack indefinitely
    // until they are spent.
    //
    // With true the trigger therefore NEVER fired. It became visible in game on
    // 15 Sep 2026: the Felsworn cast neither Azzinoth's Assault nor Sargeron
    // Smite (both `aura stacks::Felfury,2`), and Spirit Eclipse never happened
    // on the Witch Doctor.
    return botAI->GetAura(spellName, GetTarget(), false, false, StackPart()) != nullptr;
}

std::string CoaResourceTrigger::NamePart() const
{
    size_t comma = qualifier.rfind(',');
    return comma == std::string::npos ? qualifier : qualifier.substr(0, comma);
}

int CoaResourceTrigger::PercentPart() const
{
    size_t comma = qualifier.rfind(',');
    if (comma == std::string::npos)
        return 100;
    return atoi(qualifier.c_str() + comma + 1);
}

// The core resources under their own names. The compatibility module only
// carries its OWN resources (Insanity, Static, Solar Power and so on) - rage,
// energy, runic power, focus and mana come from the core and were not available
// to this trigger at all.
//
// It showed on 15 Sep 2026: Command: Undead costs 30 runic power, so the
// Necromancer bot could only cast the spender late, and we had no way to put it
// behind its builder. Same thing on the Primalist with rage.
static std::map<std::string, Powers> const CORE_RESOURCES = {
    {"mana", POWER_MANA},
    {"rage", POWER_RAGE},
    {"focus", POWER_FOCUS},
    {"energy", POWER_ENERGY},
    {"runicpower", POWER_RUNIC_POWER},
    {"runic power", POWER_RUNIC_POWER},
};

bool CoaResourceTrigger::IsActive()
{
    std::string const name = NamePart();
    if (name.empty())
        return false;

    // The Ascension-specific resources (Static, Felfury, Insanity, Solar Power
    // and the rest) are not reachable: mod-ascension-compat's public API
    // exposes specializations, talents and class abilities, but nothing to read
    // a custom resource. So only the core powers below work here.
    //
    // No rotation currently needs more - all 71 of them get by on aura stacks,
    // buff missing, debuff missing and can cast. If that changes, the shortest
    // route is asking for a percent accessor in AscensionSpecialization.h.

    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    auto itr = CORE_RESOURCES.find(lower);
    if (itr == CORE_RESOURCES.end())
        return false;  // Neither a module nor a core resource of that name

    Powers const type = itr->second;
    if (!bot->HasActivePowerType(type))
        return false;  // The class does not carry this resource

    uint32 const maximum = bot->GetMaxPower(type);
    if (!maximum)
        return false;

    return int32(bot->GetPower(type) * 100 / maximum) >= PercentPart();
}

// Wide enough for a bot to see its summons anywhere in a fight, and narrow
// enough that creatures at the other end of the zone do not count.
static constexpr float SEARCH_RANGE = 60.0f;

/* The aura of that name the buff lines look for, a passive namesake set aside. Every Draconic
 * Pyromancer wears its specialization aura (92128) under the name of its form, Draconic Aspect
 * (802117): the form counted as worn, so neither "buff missing" nor "cast buff" ever took it (#29;
 * never up on the bench either, 03/10). When the aura found by name is passive and the spell the bot
 * casts under that name is not, the one worn is that spell's own aura, if any. */
static Aura* CoaWornBuff(PlayerbotAI* botAI, std::string const& name, Unit* target, bool owner, bool duration)
{
    Aura* aura = botAI->GetAura(name, target, owner, duration);
    if (!aura || !aura->GetSpellInfo()->IsPassive())
        return aura;

    uint32 const id = botAI->GetAiObjectContext()->GetValue<uint32>("spell id", name)->Get();
    SpellInfo const* info = id ? sSpellMgr->GetSpellInfo(id) : nullptr;
    if (!info || info->IsPassive())
        return aura;

    Aura* worn = owner ? target->GetAura(id, botAI->GetBot()->GetGUID()) : target->GetAura(id);
    if (worn && duration && worn->GetDuration() == -1)
        return nullptr;
    return worn;
}

bool CoaCastBuffAction::isUseful()
{
    Unit* target = GetTarget();
    if (!target || !AI_VALUE2(uint32, "spell id", spell) || !CastSpellAction::isUseful())
        return false;
    return ai::buff::BuffBelowRefreshTarget(botAI, CoaWornBuff(botAI, spell, target, isOwner, checkDuration),
                                            beforeDuration);
}

bool CoaBuffMissingTrigger::IsActive()
{
    Unit* target = GetTarget();
    bool active = target && ai::buff::BuffBelowRefreshTarget(
                                botAI, CoaWornBuff(botAI, spell, target, checkIsOwner, checkDuration), beforeDuration);
    bool cooling = false;
    if (active)
    {
        // no id: a spell the bot has not learned (Greater Rite at level 48 read IMPOSSIBLE every tick)
        uint32 const id = AI_VALUE2(uint32, "spell id", spell);
        SpellInfo const* info = id ? sSpellMgr->GetSpellInfo(id) : nullptr;
        active = info && !CoaHealerAvoidsForm(bot, info) && CoaRotationMayCast(botAI, bot, info);
        cooling = active && bot->HasSpellCooldown(id);
    }
    return backoff.Allow(active, cooling);
}

/* How long a debuff target must still live, with AiPlayerbot.CoaShortLivedDebuffs.
 *
 * The base DebuffTrigger and CastDebuffSpellAction want 8 s of life left on the target (its health
 * over "estimated group dps"), whatever the spell. Right for a long DoT on a creature about to die;
 * wrong for the rest. A trial or trash creature never has 8 s left, so on the dungeon trials of
 * 02-03/10 only 7 of the 64 such lines of the web DPS rotations ever fired (Conjure Storm, Melt
 * Reality, Unmake, Earthquake, Hex of Malice... never cast; nuit-0310/aoe/AOE.md).
 *
 * Here the life needed follows the spell:
 *   - half its duration: the DoT pays back its global cooldown once half its ticks land
 *     (Unmake, 1.5 s: 0.75 s; Conjure Storm, 10 s: 5 s; a DoT of 16 s or more keeps the 8 s);
 *   - 2 s at most when it also hits at once (school or weapon damage, leech): that part is never
 *     lost (Hex of Malice, Nerubian Sting, Conjure Storm, Blade of Faith, Ravage);
 *   - 8 s for a permanent aura or one without a duration, as before.
 * And for a spell that strikes an area (ground effect, area around the target or the caster), the
 * life left is that of the enemies inside the area, not of the current target alone (CoaDebuffLifeTime).
 */
namespace
{
bool CoaHitsAtOnce(SpellInfo const* info)
{
    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
    {
        switch (info->Effects[i].Effect)
        {
            case SPELL_EFFECT_SCHOOL_DAMAGE:
            case SPELL_EFFECT_HEALTH_LEECH:
            case SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL:
            case SPELL_EFFECT_WEAPON_PERCENT_DAMAGE:
            case SPELL_EFFECT_WEAPON_DAMAGE:
            case SPELL_EFFECT_NORMALIZED_WEAPON_DMG:
                return true;
            default:
                break;
        }
    }
    return false;
}

float CoaDebuffNeedLifeTime(SpellInfo const* info, float base)
{
    if (!info)
        return base;

    float need = base;
    int32 const duration = info->GetMaxDuration();
    if (duration > 0)
        need = std::min(need, float(duration) / 2000.0f);
    if (CoaHitsAtOnce(info))
        need = std::min(need, 2.0f);
    return need;
}

// The radius of the spell's enemy area, and whether it is centred on the caster. 0 when it has none.
float CoaEnemyArea(Player* bot, SpellInfo const* info, bool& aroundCaster)
{
    aroundCaster = false;
    float radius = 0.0f;
    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
    {
        SpellEffectInfo const& effect = info->Effects[i];
        if (!effect.IsEffect())
            continue;

        bool const persistent = effect.IsEffect(SPELL_EFFECT_PERSISTENT_AREA_AURA);
        SpellImplicitTargetInfo const* area = nullptr;
        if (effect.TargetA.IsArea() && effect.TargetA.GetCheckType() == TARGET_CHECK_ENEMY)
            area = &effect.TargetA;
        else if (effect.TargetB.IsArea() && effect.TargetB.GetCheckType() == TARGET_CHECK_ENEMY)
            area = &effect.TargetB;
        if (!area && !persistent)
            continue;

        // Centred on the caster: an area around it or its cone, or a place set at its feet
        // (TARGET_DEST_CASTER). Otherwise on the target, where a bot puts its ground effects.
        if (area && (area->GetReferenceType() == TARGET_REFERENCE_TYPE_CASTER ||
                     area->GetReferenceType() == TARGET_REFERENCE_TYPE_SRC))
            aroundCaster = true;
        else if (effect.TargetA.GetObjectType() == TARGET_OBJECT_TYPE_DEST &&
                 effect.TargetA.GetReferenceType() == TARGET_REFERENCE_TYPE_CASTER)
            aroundCaster = true;
        radius = std::max(radius, std::max(effect.CalcRadius(bot), 5.0f));
    }
    return radius;
}

float CoaDebuffLifeTime(PlayerbotAI* botAI, Unit* target, SpellInfo const* info)
{
    AiObjectContext* context = botAI->GetAiObjectContext();
    float const dps = context->GetValue<float>("estimated group dps")->Get();
    if (dps <= 0.0f)
        return std::numeric_limits<float>::max();

    float health = float(target->GetHealth());
    bool aroundCaster = false;
    float const radius = info ? CoaEnemyArea(botAI->GetBot(), info, aroundCaster) : 0.0f;
    if (radius > 0.0f)
    {
        Unit* centre = aroundCaster ? static_cast<Unit*>(botAI->GetBot()) : target;
        for (ObjectGuid const guid : context->GetValue<GuidVector>("attackers")->Get())
        {
            if (guid == target->GetGUID())
                continue;
            Unit* unit = botAI->GetUnit(guid);
            if (unit && unit->IsAlive() && unit->IsInWorld() && unit->GetMapId() == target->GetMapId() &&
                centre->GetExactDist(unit) <= radius)
                health += float(unit->GetHealth());
        }
    }
    return health / dps;
}

bool CoaShortLivedDebuffAllowed(PlayerbotAI* botAI, Unit* target, std::string const& spell, float base)
{
    uint32 const id = botAI->GetAiObjectContext()->GetValue<uint32>("spell id", spell)->Get();
    SpellInfo const* info = id ? sSpellMgr->GetSpellInfo(id) : nullptr;
    return CoaDebuffLifeTime(botAI, target, info) >= CoaDebuffNeedLifeTime(info, base);
}

// AiPlayerbot.CoaShortLivedDebuffs: 1 for every bot, 2 for the bots whose GUID is a multiple of 4 only
// (half of the even-GUID "web" bots of the A/B bench; the other half, GUID % 4 == 2, is the control).
bool CoaShortLivedDebuffsFor(Player* bot)
{
    uint32 const mode = sPlayerbotAIConfig.coaShortLivedDebuffs;
    return mode == 1 || (mode == 2 && bot->GetGUID().GetCounter() % 4 == 0);
}
}  // namespace

// The life the base classes ask of a debuff target (DebuffTrigger, CastDebuffSpellAction defaults).
static constexpr float BASE_DEBUFF_LIFE = 8.0f;

bool CoaDebuffMissingTrigger::IsActive()
{
    bool active;
    if (CoaShortLivedDebuffsFor(bot))
    {
        // DebuffTrigger::IsActive with the life check below in place of its fixed one.
        Unit* target = GetTarget();
        active = target && target->IsAlive() && target->IsInWorld() && BuffTrigger::IsActive() &&
                 CoaShortLivedDebuffAllowed(botAI, target, spell, needLifeTime);
    }
    else
        active = DebuffTrigger::IsActive();

    bool cooling = false;
    if (active)
    {
        uint32 const id = AI_VALUE2(uint32, "spell id", spell);
        active = !CoaHealerSavesManaFrom(bot, id ? sSpellMgr->GetSpellInfo(id) : nullptr);
        cooling = active && id && bot->HasSpellCooldown(id);
    }
    return backoff.Allow(active, cooling);
}

bool CoaCastDebuffAction::isUseful()
{
    if (!CoaShortLivedDebuffsFor(bot))
        return CastDebuffSpellAction::isUseful();

    // CastDebuffSpellAction::isUseful with the life check below in place of its fixed one.
    Unit* target = GetTarget();
    if (!target || !target->IsAlive() || !target->IsInWorld())
        return false;

    return CastAuraSpellAction::isUseful() && CoaShortLivedDebuffAllowed(botAI, target, spell, BASE_DEBUFF_LIFE);
}

// What this spell would heal, all its ticks counted, and the health the group is missing around
// the bot. A heal worth far more than what is missing is a heal poured into full health.
namespace
{
float CoaHealWorth(Player* bot, SpellInfo const* info, Unit* target)
{
    float total = 0.0f;
    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
    {
        SpellEffectInfo const& effect = info->Effects[i];
        if (effect.Effect == SPELL_EFFECT_HEAL)
            total += float(std::max(0, effect.CalcValue(bot)));
        else if (effect.Effect == SPELL_EFFECT_HEAL_PCT)
            total += float(target->GetMaxHealth()) * float(std::max(0, effect.CalcValue(bot))) / 100.0f;
        else if ((effect.Effect == SPELL_EFFECT_APPLY_AURA || effect.Effect == SPELL_EFFECT_APPLY_AREA_AURA_PARTY ||
                  effect.Effect == SPELL_EFFECT_APPLY_AREA_AURA_RAID) &&
                 effect.ApplyAuraName == SPELL_AURA_PERIODIC_HEAL)
        {
            int32 const duration = info->GetMaxDuration();
            uint32 const ticks = effect.Amplitude > 0 && duration > 0 ? uint32(duration / effect.Amplitude) : 1;
            total += float(std::max(0, effect.CalcValue(bot))) * float(std::max(1u, ticks));
        }
    }
    return total;
}

float CoaMissingAround(Player* bot)
{
    float missing = float(bot->GetMaxHealth() - bot->GetHealth());
    if (Group* group = bot->GetGroup())
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            if (Player* member = ref->GetSource())
                if (member != bot && member->IsAlive() && member->IsInWorld() && bot->GetDistance(member) < 30.0f)
                    missing += float(member->GetMaxHealth() - member->GetHealth());
    return missing;
}
}  // namespace

// A summon line that stays true does not bring the summon: a Knight of Xoroth stood 170 s in an Arathi Basin
// on "can cast::Call: Hellfire Imp", recast without end and never in combat (CoA test realm, 06/10). Ten
// seconds without the summon standing, the line is set aside 30 s, as rotation buff lines are.
bool CoaCanCastTrigger::IsActive()
{
    bool const active = CanCastNow();
    uint32 const id = AI_VALUE2(uint32, "spell id", spell);
    SpellInfo const* info = id ? sSpellMgr->GetSpellInfo(id) : nullptr;
    if (!info || !info->HasEffect(SPELL_EFFECT_SUMMON))
        return active;
    return summonBackoff.Allow(active);
}

bool CoaCanCastTrigger::CanCastNow()
{
    if (!SpellCanBeCastTrigger::IsActive())
        return false;

    uint32 const id = AI_VALUE2(uint32, "spell id", spell);
    SpellInfo const* info = id ? sSpellMgr->GetSpellInfo(id) : nullptr;
    if (!info)
        return true;

    // A resurrection in a rotation line is aimed at the current target, an enemy (Spiritual Ascension,
    // Prayer Beads: jealous-sound/azerothcore-wotlk-coa#5732, #5633). The dead are raised by "coa resurrect".
    for (SpellEffectInfo const& effect : info->Effects)
        if (effect.Effect == SPELL_EFFECT_RESURRECT || effect.Effect == SPELL_EFFECT_RESURRECT_NEW)
            return false;

    // A rotation line asks for a heal whenever it is off cooldown, whoever needs it. When it is
    // worth three times what the group is missing, it would land on full health: not now.
    if (float const worth = CoaHealWorth(bot, info, bot))
        if (worth > CoaMissingAround(bot) * 3.0f)
            return false;

    // A spell that burns or drains a power the target does not have does nothing: a Necromancer's
    // Glacial Tap (Power Burn) was refused 51 times in a row on dungeon trash without mana (23/09).
    for (SpellEffectInfo const& effect : info->Effects)
        if (effect.Effect == SPELL_EFFECT_POWER_BURN || effect.Effect == SPELL_EFFECT_POWER_DRAIN)
            if (Unit* target = AI_VALUE(Unit*, "current target"))
                if (target->GetMaxPower(Powers(effect.MiscValue)) == 0)
                    return false;

    // A boss is never charmed: Enslave Elemental, a Felsworn rotation line, charmed Noxxion in Maraudon and
    // broke the encounter (jealous-sound/azerothcore-wotlk-coa#4835). Hellbound Leash (Knight of Xoroth) too.
    if (Unit* target = AI_VALUE(Unit*, "current target"))
        if (Creature* creature = target->ToCreature())
            if (creature->IsDungeonBoss() || creature->isWorldBoss())
                for (SpellEffectInfo const& effect : info->Effects)
                    if (effect.IsAura() && (effect.ApplyAuraName == SPELL_AURA_MOD_CHARM ||
                                            effect.ApplyAuraName == SPELL_AURA_MOD_POSSESS ||
                                            effect.ApplyAuraName == SPELL_AURA_AOE_CHARM))
                        return false;

    int32 const duration = info->GetMaxDuration();
    if (bot->HasAura(id) && (duration < 0 || duration > 60 * IN_MILLISECONDS))
        return false;

    // Travel utility has no place in a rotation: Grace of the Moon, a water walk at 40% of base mana
    // that any damage cancels, took 79% of a Starcaller healer's mana in one fight. Looked for in the
    // spell a CoA ability triggers too: Tinker's Parachute Pack hands its slow fall out through
    // another spell, and bots kept putting parachutes on the group.
    auto const travelUtility = [](SpellInfo const* spell)
    {
        for (SpellEffectInfo const& effect : spell->Effects)
            if (effect.Effect == SPELL_EFFECT_APPLY_AURA || effect.Effect == SPELL_EFFECT_APPLY_AREA_AURA_PARTY ||
                effect.Effect == SPELL_EFFECT_APPLY_AREA_AURA_RAID)
                switch (effect.ApplyAuraName)
                {
                    case SPELL_AURA_WATER_WALK: case SPELL_AURA_FEATHER_FALL: case SPELL_AURA_HOVER:
                    case SPELL_AURA_WATER_BREATHING:
                        return true;
                    default:
                        break;
                }
        return false;
    };
    if (travelUtility(info))
        return false;

    bool summons = false;
    std::vector<uint32> summoned;
    for (SpellEffectInfo const& effect : info->Effects)
    {
        if (effect.Effect == SPELL_EFFECT_TRIGGER_SPELL && effect.TriggerSpell)
            if (SpellInfo const* triggered = sSpellMgr->GetSpellInfo(effect.TriggerSpell))
                if (travelUtility(triggered))
                    return false;
        if (effect.Effect == SPELL_EFFECT_SUMMON)
        {
            summons = true;
            if (effect.MiscValue > 0)
                summoned.push_back(uint32(effect.MiscValue));
        }
    }

    // A ward or effigy of which only one may stand: not again while the bot's own still stands
    // (Healing Ward was put down 15 times in one fight, 18% of base mana each). The Cultist's summons
    // come from a script, without UNIT_CREATED_BY_SPELL: the creature the spell names counts too.
    // Tentacle of Yogg-Saron was still recast all fight long in 1.7
    // (jealous-sound/azerothcore-wotlk-coa#6093, after #5739).
    auto const sameSummon = [&summoned](Unit const* unit)
    { return std::find(summoned.begin(), summoned.end(), unit->GetEntry()) != summoned.end(); };
    if (summons && getMSTimeDiff(summonCheckedAt, getMSTime()) < 3 * IN_MILLISECONDS && summonCheckedAt)
    {
        if (summonStanding)
            return false;
    }
    else if (summons)
    {
        summonCheckedAt = getMSTime();
        summonStanding = false;
        std::list<Unit*> nearby;
        Acore::AnyUnitInObjectRangeCheck check(bot, SEARCH_RANGE);
        Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> search(bot, nearby, check);
        Cell::VisitObjects(bot, search, SEARCH_RANGE);
        for (Unit* unit : nearby)
            if (unit && unit->IsAlive() && !unit->IsPlayer() &&
                (unit->GetUInt32Value(UNIT_CREATED_BY_SPELL) == id || sameSummon(unit)) &&
                (unit->GetOwnerGUID() == bot->GetGUID() || unit->GetCreatorGUID() == bot->GetGUID()))
            {
                summonStanding = true;
                return false;
            }
    }

    return !CoaHealerSavesManaFrom(bot, info) && !CoaHealerAvoidsForm(bot, info) &&
           CoaRotationMayCast(botAI, bot, info);
}

void CoaSummonMissingTrigger::Qualify(std::string const qual)
{
    auto const blank = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    std::string name = qual;
    name.erase(std::find_if_not(name.rbegin(), name.rend(), blank).base(), name.end());
    name.erase(name.begin(), std::find_if_not(name.begin(), name.end(), blank));
    Qualified::Qualify(name);
    entry = 0;
    nameKey.clear();
    if (name.empty())
        return;

    if (std::all_of(name.begin(), name.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
    {
        entry = uint32(std::strtoul(name.c_str(), nullptr, 10));
        if (!sObjectMgr->GetCreatureTemplate(entry))
            entry = 0;
    }
    else
        nameKey = CoaNameKey(name);
}

// With a qualifier, only the creature it names; without one, any creature of the bot.
bool CoaSummonMissingTrigger::Counts(Unit* unit) const
{
    if (qualifier.empty())
        return true;

    if (entry)
        return unit->GetEntry() == entry;

    return CoaNameIs(unit->GetName(), nameKey);
}

bool CoaSummonMissingTrigger::IsActive()
{
    // A qualifier that is neither a known creature entry nor valid UTF-8: stay quiet rather than summon forever.
    // A misspelt name is not caught here and keeps the trigger on.
    if (!qualifier.empty() && !entry && nameKey.empty())
        return false;

    for (Unit* unit : bot->m_Controlled)
        if (unit && unit->IsAlive() && unit->GetOwnerGUID() == bot->GetGUID() && Counts(unit))
            return false;

    // NOT through AI_VALUE("nearest npcs"): that value is cached. Freshly
    // summoned creatures only appear in it after the next refresh, and during
    // that window the trigger keeps the way clear - which is how the Witch
    // Doctor put down a whole field of Serpent Wards on 15 Sep 2026. Hence our
    // own, uncached search here.
    std::list<Unit*> nearby;
    Acore::AnyUnitInObjectRangeCheck check(bot, SEARCH_RANGE);
    Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> search(bot, nearby, check);
    Cell::VisitObjects(bot, search, SEARCH_RANGE);

    for (Unit* unit : nearby)
        if (unit && unit->IsAlive() && !unit->IsPlayer() &&
            (unit->GetOwnerGUID() == bot->GetGUID() ||
             (!qualifier.empty() && unit->GetCreatorGUID() == bot->GetGUID())) &&
            Counts(unit))
            return false;

    return true;
}

bool CoaSpellReadyTrigger::IsActive()
{
    if (qualifier.empty())
        return false;
    // as in SpellNoCooldownTrigger::IsActive
    uint32 spellId = AI_VALUE2(uint32, "spell id", qualifier);
    if (!spellId)
        return false;

    return !bot->HasSpellCooldown(spellId);
}
