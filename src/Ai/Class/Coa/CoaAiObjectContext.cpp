/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaAiObjectContext.h"

#include "Action.h"
#include "AttackAction.h"
#include "CoaDungeonPriorities.h"
#include "CoaSpecialization.h"
#include "CoaWildcard.h"
#include "CombatStrategy.h"
#include "DatabaseEnv.h"
#include "Engine.h"
#include "Group.h"
#include "MovementActions.h"
#include "NamedObjectContext.h"
#include "NonCombatStrategy.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "ServerFacade.h"
#include "Spell.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Strategy.h"
#include "TemporarySummon.h"
#include "Trigger.h"
#include "AscensionSpecialization.h"

#include <algorithm>
#include <numeric>
#include <tuple>
#include <array>
#include <atomic>
#include <cmath>
#include <iterator>
#include <ctime>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <tuple>
#include <vector>

// CoaBossKnowledge.cpp: "coa boss dodge", out of the way of the casts of dungeon and raid enemies.
Strategy* NewCoaBossDodgeStrategy(PlayerbotAI* botAI);
Action* NewCoaBossDodgeAction(PlayerbotAI* botAI);
Trigger* NewCoaBossDangerTrigger(PlayerbotAI* botAI);

// CoaPvpTactics.cpp: "coa pvp", arenas, duels and battlegrounds.
NamedObjectContext<Strategy>* NewCoaPvpStrategyContext();
NamedObjectContext<Action>* NewCoaPvpActionContext();
NamedObjectContext<Trigger>* NewCoaPvpTriggerContext();

// CoaGroupTelemetry.cpp: counts a taunt, a heal tried, or an interrupt, in the group fight being measured.
void CoaTelemetryNoteTaunt(Player* bot);
void CoaTelemetryNoteHeal(Player* bot, uint32 spellId, uint16 outcome);
void CoaTelemetryNoteInterrupt(Player* bot, bool wasted);

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
    KIND_RESURRECT  = 0x4000,  // brings a dead ally back
    KIND_SHARE_HARM = 0x8000   // the caster takes part of the damage its target takes
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
    uint32 purgeMask = 0;  // magic buffs and enrages it takes off an enemy (Purge, Tranquilizing Shot)
    uint32 synergy = 0;   // a Wildcard Hero's: how well the rolled entry fits the rest of its build
};

struct ClassKit
{
    std::vector<CoaAbility> abilities;
    uint16 kinds = 0;  // every kind some ability of the class has
    uint32 purgeMask = 0;  // what some ability of the class takes off an enemy
};

// Ally targets: pet, party and raid areas, ally or any unit, chain heal, nearby ally, party or raid
// member and allies in a cone.
bool IsAllyTarget(uint32 target)
{
    switch (target)
    {
        case 3: case 4: case 5: case 20: case 21: case 25: case 30: case 31: case 33:
        case 34: case 35: case 37: case 45: case 56: case 57: case 58: case 59: case 61:
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

// A spell that hands its caster a creature to steer from the client: the caster stands still meanwhile,
// and a bot steers nothing.
bool PossessesTarget(SpellInfo const* info)
{
    for (SpellEffectInfo const& effect : info->Effects)
        if (IsAuraEffect(effect) && effect.ApplyAuraName == SPELL_AURA_MOD_POSSESS &&
            effect.TargetA.GetTarget() != TARGET_UNIT_CASTER)
            return true;
    return false;
}

// Whether the control a spell puts on an enemy ends with the first damage it takes (a sap, a sleep, a
// polymorph), looking into the spells it triggers.
bool DamageEndsControl(SpellInfo const* info, uint8 depth = 0)
{
    for (SpellEffectInfo const& effect : info->Effects)
    {
        if (IsAuraEffect(effect) && IsControlAura(effect) && effect.TargetA.GetTarget() != TARGET_UNIT_CASTER &&
            (info->AuraInterruptFlags & AURA_INTERRUPT_FLAG_TAKE_DAMAGE))
            return true;
        if (effect.TriggerSpell && effect.TriggerSpell != info->Id && depth < 2)
            if (SpellInfo const* triggered = sSpellMgr->GetSpellInfo(effect.TriggerSpell))
                if (DamageEndsControl(triggered, depth + 1))
                    return true;
    }
    return false;
}

// Whether the spell makes an enemy immune to damage or banishes it, looking into the spells it triggers.
bool ShieldsEnemy(SpellInfo const* info, uint8 depth = 0)
{
    if (info->Mechanic == MECHANIC_BANISH)
        return true;
    for (SpellEffectInfo const& effect : info->Effects)
    {
        if (IsAuraEffect(effect) && effect.TargetA.GetTarget() != TARGET_UNIT_CASTER &&
            (effect.Mechanic == MECHANIC_BANISH || effect.ApplyAuraName == SPELL_AURA_SCHOOL_IMMUNITY ||
             effect.ApplyAuraName == SPELL_AURA_DAMAGE_IMMUNITY))
            return true;
        if (effect.TriggerSpell && effect.TriggerSpell != info->Id && depth < 2)
            if (SpellInfo const* triggered = sSpellMgr->GetSpellInfo(effect.TriggerSpell))
                if (ShieldsEnemy(triggered, depth + 1))
                    return true;
    }
    return false;
}

// Whether the spell would charm or possess a dungeon, raid or world boss: Enslave Elemental (Felsworn)
// charmed Noxxion in Maraudon and broke the encounter (jealous-sound/azerothcore-wotlk-coa#4835).
bool TakesOverBoss(SpellInfo const* info, Unit* target)
{
    Creature* creature = target ? target->ToCreature() : nullptr;
    if (!creature || !(creature->IsDungeonBoss() || creature->isWorldBoss()))
        return false;
    for (SpellEffectInfo const& effect : info->Effects)
        if (IsAuraEffect(effect) && (effect.ApplyAuraName == SPELL_AURA_MOD_CHARM ||
                                     effect.ApplyAuraName == SPELL_AURA_MOD_POSSESS ||
                                     effect.ApplyAuraName == SPELL_AURA_AOE_CHARM))
            return true;
    return false;
}

// A spell that sends its caster to its home inn, the way a hearthstone does.
bool TeleportsCasterHome(SpellInfo const* info)
{
    for (SpellEffectInfo const& effect : info->Effects)
        if (effect.Effect == SPELL_EFFECT_TELEPORT_UNITS &&
            (effect.TargetA.GetTarget() == TARGET_DEST_HOME || effect.TargetB.GetTarget() == TARGET_DEST_HOME))
            return true;
    return false;
}

// A spell that keeps its caster rolling or running forward for its whole duration, unable to stop or to steer
// (Boulder Dash, Advance, Shooting Star).
bool ForcesCasterForward(SpellInfo const* info)
{
    for (SpellEffectInfo const& effect : info->Effects)
        if (IsAuraEffect(effect) && effect.ApplyAuraName == SPELL_AURA_FORCE_MOVE_FORWARD &&
            effect.TargetA.GetTarget() == TARGET_UNIT_CASTER)
            return true;
    return false;
}

// A spell that sends its caster to a place in front of it (Primal Rush rushes 20 yards on through the enemies).
bool SendsCasterAhead(SpellInfo const* info)
{
    for (SpellEffectInfo const& effect : info->Effects)
    {
        if (effect.Effect != SPELL_EFFECT_CHARGE_DEST && effect.Effect != SPELL_EFFECT_LEAP &&
            effect.Effect != SPELL_EFFECT_JUMP_DEST && effect.Effect != SPELL_EFFECT_TELEPORT_UNITS)
            continue;
        for (uint32 const dest : { effect.TargetA.GetTarget(), effect.TargetB.GetTarget() })
            if (dest == TARGET_DEST_CASTER_FRONT || dest == TARGET_DEST_CASTER_FRONT_LEAP)
                return true;
    }
    return false;
}

// A spell that moves its caster somewhere else: a jump, leap, charge or blink (Dragon Leap jumps to its target),
// looking into the spells the cast itself triggers.
bool MovesCaster(SpellInfo const* info, uint8 depth = 0)
{
    for (SpellEffectInfo const& effect : info->Effects)
    {
        switch (effect.Effect)
        {
            case SPELL_EFFECT_JUMP: case SPELL_EFFECT_JUMP_DEST: case SPELL_EFFECT_CHARGE:
            case SPELL_EFFECT_CHARGE_DEST: case SPELL_EFFECT_LEAP: case SPELL_EFFECT_LEAP_BACK:
                return true;
            case SPELL_EFFECT_TELEPORT_UNITS:
                if (effect.TargetA.GetTarget() == TARGET_UNIT_CASTER)
                    return true;
                break;
            default:
                break;
        }
        if (effect.TriggerSpell && effect.TriggerSpell != info->Id && !IsAuraEffect(effect) && depth < 2)
            if (SpellInfo const* triggered = sSpellMgr->GetSpellInfo(effect.TriggerSpell))
                if (MovesCaster(triggered, depth + 1))
                    return true;
    }
    return false;
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
 * A transform that speeds its bearer up is a form to ride in, not to fight in (Bloodmage
 * Running Wild 800175, Reaper Ghost Form 561083): it cannot be cast in combat and ends with
 * the next cast or swing, so a bot would put it back on between any two spells.
 */
bool IsStance(SpellInfo const* info)
{
    if (info->GetMaxDuration() > 0)
        return false;

    bool aura = false;
    bool transform = false;
    bool speed = false;
    for (SpellEffectInfo const& effect : info->Effects)
    {
        if (!effect.IsEffect())
            continue;

        // A summon, a teleport, a trade skill or an item alongside the aura: not a stance.
        if (!IsAuraEffect(effect) || effect.ApplyAuraName == SPELL_AURA_NONE ||
            effect.TargetA.GetTarget() != TARGET_UNIT_CASTER)
            return false;

        aura = true;
        transform |= effect.ApplyAuraName == SPELL_AURA_TRANSFORM;
        speed |= effect.ApplyAuraName == SPELL_AURA_MOD_INCREASE_SPEED ||
                 effect.ApplyAuraName == SPELL_AURA_MOD_SPEED_ALWAYS ||
                 effect.ApplyAuraName == SPELL_AURA_MOD_SPEED_NOT_STACK;
    }

    return aura && !(transform && speed);
}

/*
 * A stealth or an invisibility on the caster (Ranger Elude 801345, Venomancer Skulk 800843, Reaper
 * Underwalk 800797, Runemaster Runeshroud 500288). IsStance takes them for stances, and "coa buff"
 * kept the bot hidden and slowed down out of combat, in towns and between trial waves, in place of
 * its real stance. With AiPlayerbot.CoaStealthIsNotStance they are left to the rotations.
 */
bool HidesCaster(SpellInfo const* info)
{
    for (SpellEffectInfo const& effect : info->Effects)
        if (effect.IsAura(SPELL_AURA_MOD_STEALTH) || effect.IsAura(SPELL_AURA_MOD_INVISIBILITY))
            return true;
    return false;
}

// A stance or buff that raises the threat its bearer causes: a tank's. Read in every form CoA uses (a threat
// percent, a modifier of the threat of its spells, a flat threat) and in the spells it triggers, where CoA
// abilities often carry their aura.
bool RaisesThreat(SpellInfo const* info, uint8 depth = 0)
{
    for (SpellEffectInfo const& effect : info->Effects)
    {
        if (effect.IsAura() && effect.CalcValue() > 0 &&
            (effect.ApplyAuraName == SPELL_AURA_MOD_THREAT || effect.ApplyAuraName == SPELL_AURA_MOD_TOTAL_THREAT ||
             ((effect.ApplyAuraName == SPELL_AURA_ADD_PCT_MODIFIER || effect.ApplyAuraName == SPELL_AURA_ADD_FLAT_MODIFIER) &&
              effect.MiscValue == SPELLMOD_THREAT)))
            return true;
        if (depth < 2 && effect.TriggerSpell && effect.Effect == SPELL_EFFECT_TRIGGER_SPELL)
            if (SpellInfo const* triggered = sSpellMgr->GetSpellInfo(effect.TriggerSpell))
                if (triggered != info && RaisesThreat(triggered, depth + 1))
                    return true;
    }
    return false;
}

// An ability causing more threat than its damage: a flat threat effect (Spiked Reinforcement) or a threat entry of
// the core. The Guardian and the Venomancer have no threat stance: their aggro comes from these.
bool BuildsThreat(SpellInfo const* info)
{
    for (SpellEffectInfo const& effect : info->Effects)
        if (effect.Effect == SPELL_EFFECT_THREAT && effect.CalcValue() > 0)
            return true;
    SpellThreatEntry const* entry = sSpellMgr->GetSpellThreatEntry(info->Id);
    return entry && (entry->flatMod > 0 || entry->pctMod > 1.0f || entry->apPctMod > 0.0f);
}

// Whether the spell deals damage, itself or by a spell it triggers: a weapon strike, a school damage, a damage over time.
bool DealsDamage(SpellInfo const* info, uint8 depth = 0)
{
    for (SpellEffectInfo const& effect : info->Effects)
    {
        switch (effect.Effect)
        {
            case SPELL_EFFECT_SCHOOL_DAMAGE:
            case SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL:
            case SPELL_EFFECT_WEAPON_PERCENT_DAMAGE:
            case SPELL_EFFECT_WEAPON_DAMAGE:
            case SPELL_EFFECT_NORMALIZED_WEAPON_DMG:
            case SPELL_EFFECT_HEALTH_LEECH:
                return true;
            default:
                break;
        }
        if (effect.IsAura() && (effect.ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE ||
                                effect.ApplyAuraName == SPELL_AURA_PERIODIC_LEECH))
            return true;
        if (depth < 2 && effect.TriggerSpell &&
            (effect.Effect == SPELL_EFFECT_TRIGGER_SPELL || effect.Effect == SPELL_EFFECT_TRIGGER_MISSILE))
            if (SpellInfo const* triggered = sSpellMgr->GetSpellInfo(effect.TriggerSpell))
                if (triggered != info && DealsDamage(triggered, depth + 1))
                    return true;
    }
    return false;
}

// Swimming and breathing under water: a form to travel in, not to stand in.
bool IsTravelForm(SpellInfo const* info)
{
    for (SpellEffectInfo const& effect : info->Effects)
        if (effect.IsAura() && (effect.ApplyAuraName == SPELL_AURA_MOD_INCREASE_SWIM_SPEED ||
                                effect.ApplyAuraName == SPELL_AURA_WATER_BREATHING))
            return true;
    return false;
}

// Whether the bot stands in this stance or in another of its family: any rank of it, or a stance of
// the same spell category. Runic Tattoos share category 200 and come in several ranks; looking for the
// exact spell id, the bot thought it wore none and put on a second tattoo, then a third (#5059).
bool StandsIn(Player* bot, SpellInfo const* stance)
{
    uint32 const category = stance->GetCategory();
    for (auto const& [id, application] : bot->GetAppliedAuras())
    {
        Aura const* aura = application->GetBase();
        if (aura->GetCasterGUID() != bot->GetGUID() || aura->GetSpellInfo()->IsPassive())
            continue;
        SpellInfo const* worn = aura->GetSpellInfo();
        if (worn->Id == stance->Id || (category && worn->GetCategory() == category) ||
            (worn->SpellName[0] && stance->SpellName[0] && !strcmp(worn->SpellName[0], stance->SpellName[0])))
            return true;
    }
    return false;
}

// Whether the unit wears this buff, its "Greater" form, or the plain form of a "Greater" one, from anyone.
bool WearsBuffNamed(Unit* unit, SpellInfo const* buff)
{
    if (!buff->SpellName[0] || !*buff->SpellName[0])
        return false;
    std::string name(buff->SpellName[0]);
    std::string const greater = "Greater ";
    if (name.rfind(greater, 0) == 0)
        name = name.substr(greater.size());
    for (auto const& [id, application] : unit->GetAppliedAuras())
    {
        SpellInfo const* worn = application->GetBase()->GetSpellInfo();
        if (!worn->SpellName[0])
            continue;
        std::string wornName(worn->SpellName[0]);
        if (wornName.rfind(greater, 0) == 0)
            wornName = wornName.substr(greater.size());
        // A passive namesake is no buff worn: the Draconic specialization aura 92128 bears the name of the
        // form, Draconic Aspect (802117), and the form was never taken (#29).
        if (wornName == name && worn->IsPositive() && (!worn->IsPassive() || buff->IsPassive()))
            return true;
    }
    return false;
}

// The family flags a CoA class gives the buffs a caster keeps one of on each target, read on the
// highest rank: Woodsman's Adaptation rank 1 (800266) carries none, its other ranks carry 0x200.
flag96 BuffFamilyFlags(SpellInfo const* buff)
{
    if (buff->SpellFamilyFlags)
        return buff->SpellFamilyFlags;
    if (SpellInfo const* last = sSpellMgr->GetSpellInfo(sSpellMgr->GetLastSpellInChain(buff->Id)))
        return last->SpellFamilyFlags;
    return buff->SpellFamilyFlags;
}

// Whether the unit wears another buff of the caster sharing the family flags of this one: the
// Pyromancer's Seal of Al'ar and Seal of Alysrazor (family 30, 0x10000) and the Ranger's Woodsman's
// and Footpad's Adaptation (family 27, 0x200) are one seal per caster, and a bot put both of each
// pair on the same player (jealous-sound/azerothcore-wotlk-coa#5971).
bool WearsRivalBuffFrom(Unit* unit, Unit* caster, SpellInfo const* buff)
{
    if (buff->SpellFamilyName <= SPELLFAMILY_PET)
        return false;
    flag96 const flags = BuffFamilyFlags(buff);
    if (!flags)
        return false;
    for (auto const& [id, application] : unit->GetAppliedAuras())
    {
        Aura const* aura = application->GetBase();
        SpellInfo const* worn = aura->GetSpellInfo();
        if (aura->GetCasterGUID() != caster->GetGUID() || worn->IsPassive() || !application->IsPositive() ||
            worn->SpellFamilyName != buff->SpellFamilyName ||
            sSpellMgr->GetFirstSpellInChain(worn->Id) == sSpellMgr->GetFirstSpellInChain(buff->Id))
            continue;
        if (BuffFamilyFlags(worn) & flags)
            return true;
    }
    return false;
}

// Whether the unit wears this buff from the caster, under its own id or that of a spell it triggers.
bool WearsBuffFrom(Unit* unit, Unit* caster, SpellInfo const* buff, uint8 depth = 0)
{
    if (unit->GetAura(buff->Id, caster->GetGUID()))
        return true;
    if (depth < 2)
        for (SpellEffectInfo const& effect : buff->Effects)
            if (effect.TriggerSpell)
                if (SpellInfo const* triggered = sSpellMgr->GetSpellInfo(effect.TriggerSpell))
                    if (WearsBuffFrom(unit, caster, triggered, depth + 1))
                        return true;
    return false;
}

// A buff the unit wears that the world database makes exclusive with this spell
// (spell_group_stack_rules): any caster's for SPELL_GROUP_STACK_RULE_EXCLUSIVE, the caster's own for
// SPELL_GROUP_STACK_RULE_EXCLUSIVE_FROM_SAME_CASTER (Stormbringer Aegis 111010 and Pressures 1170,
// Barbarian shouts 1210, Starcaller Aspects 1127). Casting the spell would remove it. nullptr if none.
Aura const* WornExclusiveSibling(Unit* unit, Unit* caster, SpellInfo const* spell)
{
    uint32 const first = spell->GetFirstRankSpell()->Id;
    for (auto const& [id, application] : unit->GetAppliedAuras())
    {
        Aura const* aura = application->GetBase();
        SpellInfo const* worn = aura->GetSpellInfo();
        if (worn->IsPassive() || worn->GetFirstRankSpell()->Id == first)
            continue;
        switch (sSpellMgr->CheckSpellGroupStackRules(spell, worn))
        {
            case SPELL_GROUP_STACK_RULE_EXCLUSIVE:
                return aura;
            case SPELL_GROUP_STACK_RULE_EXCLUSIVE_FROM_SAME_CASTER:
                if (aura->GetCasterGUID() == caster->GetGUID())
                    return aura;
                break;
            default:
                break;
        }
    }
    return nullptr;
}

// First rank of the member of this spell's exclusive family the bot's rotation keeps: the one named by
// the highest priority line that names a member, in its "buff missing" or "can cast" trigger or in its
// "cast buff" action, the combat and non-combat rotations alike (a Templar Oathkeeper's "medium aoe>cast
// buff::Libram of Consecration!92" over its "buff missing::Libram of Tenacity!90"). A plain "cast" action
// aims at the current target, an enemy in combat, so it never puts the buff on the bot and does not
// count (a Tinker Invention's "low mana>cast::Mana Module!87"). Taken from the
// priorities, not from the order the lines are looked at in, so a lower line never takes the family over,
// even when the kept buff is already worn. A form the bot's heals forbid is passed over: the Spider Form
// line of a Venomancer Vizier never fires, and its Vizier Form is left to "coa buff". 0 when no line
// names one, or the spell is in no spell group.
uint32 RotationKeptMember(PlayerbotAI* botAI, SpellInfo const* spell)
{
    uint32 const first = spell->GetFirstRankSpell()->Id;
    SpellSpellGroupMapBounds const groups = sSpellMgr->GetSpellSpellGroupMapBounds(first);
    CoaAiObjectContext* const context = dynamic_cast<CoaAiObjectContext*>(botAI->GetAiObjectContext());
    if (groups.first == groups.second || !context)
        return 0;

    Engine* const engines[] = { botAI->GetEngine(BOT_STATE_COMBAT), botAI->GetEngine(BOT_STATE_NON_COMBAT) };
    std::array<uint32, 2> const stamps = { engines[0] ? engines[0]->GetInitStamp() : 0u,
                                           engines[1] ? engines[1]->GetInitStamp() : 0u };
    if (stamps != context->rotationBuffsStamps)
    {
        context->rotationBuffsStamps = stamps;
        std::map<std::string, float> lines;
        auto const note = [&lines](std::string const& name, float priority)
        {
            auto const [itr, added] = lines.emplace(name, priority);
            if (!added)
                itr->second = std::max(itr->second, priority);
        };
        for (Engine* const engine : engines)
            if (engine)
                for (TriggerNode* node : engine->GetTriggerNodes())
                {
                    float priority = 0.0f;
                    for (NextAction const& action : node->getHandlers())
                    {
                        priority = std::max(priority, action.getRelevance());
                        std::string const& name = action.GetNameRef();
                        size_t const qualifier = name.find("::");
                        if (name.compare(0, 9, "cast buff") == 0 && qualifier != std::string::npos)
                            note(name.substr(qualifier + 2), action.getRelevance());
                    }
                    std::string const name = node->getName();
                    for (std::string const prefix : { "buff missing::", "can cast::" })
                        if (name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0)
                            note(name.substr(prefix.size()), priority);
                }

        context->rotationBuffs.clear();
        for (auto const& [name, priority] : lines)
        {
            uint32 const id = context->GetValue<uint32>("spell id", name)->Get();
            SpellInfo const* named = id ? sSpellMgr->GetSpellInfo(id) : nullptr;
            if (!named)
                continue;
            SpellSpellGroupMapBounds const namedGroups = sSpellMgr->GetSpellSpellGroupMapBounds(named->GetFirstRankSpell()->Id);
            if (namedGroups.first != namedGroups.second)
                context->rotationBuffs.emplace_back(name, priority);
        }
        std::stable_sort(context->rotationBuffs.begin(), context->rotationBuffs.end(),
                         [](auto const& a, auto const& b) { return a.second > b.second; });
    }

    for (auto const& [name, priority] : context->rotationBuffs)
    {
        uint32 const id = context->GetValue<uint32>("spell id", name)->Get();
        SpellInfo const* named = id ? sSpellMgr->GetSpellInfo(id) : nullptr;
        if (!named)
            continue;
        uint32 const namedFirst = named->GetFirstRankSpell()->Id;
        if (namedFirst != first)
        {
            SpellGroupStackRule const rule = sSpellMgr->CheckSpellGroupStackRules(spell, named);
            if (rule != SPELL_GROUP_STACK_RULE_EXCLUSIVE && rule != SPELL_GROUP_STACK_RULE_EXCLUSIVE_FROM_SAME_CASTER)
                continue;
        }
        if (!CoaHealerAvoidsForm(botAI->GetBot(), named))
            return namedFirst;
    }
    return 0;
}

// Whether the bot's rotation keeps another member of this spell's exclusive family on the bot: "coa
// buff" then leaves the family to the rotation there.
bool RotationKeepsSibling(PlayerbotAI* botAI, SpellInfo const* spell)
{
    uint32 const kept = RotationKeptMember(botAI, spell);
    return kept && kept != spell->GetFirstRankSpell()->Id;
}

// Whether a "buff missing::<name>" line of the bot's rotation asks for this spell.
bool RotationAsksBuff(PlayerbotAI* botAI, SpellInfo const* spell)
{
    std::string const name = spell->SpellName[LOCALE_enUS];
    std::string const line = "buff missing::" + name;
    for (BotState state : { BOT_STATE_COMBAT, BOT_STATE_NON_COMBAT })
        if (Engine* engine = botAI->GetEngine(state))
            for (TriggerNode* node : engine->GetTriggerNodes())
                if (node->getName() == line)
                    return true;
    return false;
}

// Whether the bot wears a stance of its own that raises its threat.
bool WearsThreatStance(Player* bot)
{
    for (auto const& [id, application] : bot->GetAppliedAuras())
    {
        Aura const* aura = application->GetBase();
        if (aura->GetCasterGUID() == bot->GetGUID() && !aura->GetSpellInfo()->IsPassive() &&
            RaisesThreat(aura->GetSpellInfo()))
            return true;
    }
    return false;
}

// What a spell does, looking two levels into the spells it triggers: CoA abilities often
// carry their heal, taunt or aura in a triggered spell.
void Classify(SpellInfo const* info, CoaAbility& ability, uint8 depth = 0)
{
    constexpr int32 LongAura = 5 * MINUTE * IN_MILLISECONDS;
    int32 const duration = info->GetMaxDuration();

    // Only the ability itself: a spell it triggers is not the stance the bot stands in.
    if (!depth && IsStance(info) && !(sPlayerbotAIConfig.coaStealthIsNotStance && HidesCaster(info)))
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

        if (aura && !self && effect.ApplyAuraName == SPELL_AURA_SPLIT_DAMAGE_PCT)
            ability.kind |= KIND_SHARE_HARM;

        if (effect.Effect == SPELL_EFFECT_ATTACK_ME || (aura && effect.ApplyAuraName == SPELL_AURA_MOD_TAUNT))
            ability.kind |= KIND_TAUNT;

        if (effect.Effect == SPELL_EFFECT_DISPEL && (ally || self))
        {
            ability.kind |= KIND_DISPEL;
            ability.dispelMask |= SpellInfo::GetDispelMask(DispelType(effect.MiscValue));
            // Cleansing ground, like a Runemaster's Resonance Rune (allies within 6 yards of the spot): it
            // can be put under another group member, CastSpell aims it at that member's position.
            if (targetA == TARGET_UNIT_DEST_AREA_ALLY)
                ability.kind |= KIND_ALLY_CAST;
        }

        // Purge, Spellsteal, Tranquilizing Shot and their CoA kind take a buff off an enemy: kept
        // apart from the cleanses, for "coa purge" (only magic and enrages: what a boss casts on itself).
        if ((effect.Effect == SPELL_EFFECT_DISPEL || effect.Effect == SPELL_EFFECT_STEAL_BENEFICIAL_BUFF) && enemy)
            ability.purgeMask |= SpellInfo::GetDispelMask(DispelType(effect.MiscValue)) &
                                 ((1 << DISPEL_MAGIC) | (1 << DISPEL_ENRAGE));

        if (effect.Effect == SPELL_EFFECT_INTERRUPT_CAST ||
            (aura && enemy && effect.ApplyAuraName == SPELL_AURA_MOD_SILENCE))
            ability.kind |= KIND_INTERRUPT;

        if (aura && self && IsDefensiveAura(effect) && duration > 0 && duration < LongAura)
            ability.kind |= KIND_DEFENSIVE;

        // A heal only for the caster (Barbarian, several tanks) matched no kind, so no bot ever cast
        // it: it is used like the other self-saving spells, when the bot is in trouble.
        if (heal && self && !ally)
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

    // A heal that also moves part of the target's damage onto the caster is no heal to spread around:
    // Earthmother's Binding (Primalist, 805107) passes 30% of what the target takes to the caster
    // (807858), and a Primalist bot binding itself to the tank in the middle of a pack died in two
    // blows (jealous-sound/azerothcore-wotlk-coa#5844, #5846).
    if (!depth && (ability.kind & KIND_SHARE_HARM))
        ability.kind &= ~(KIND_HEAL | KIND_GROUP_HEAL | KIND_HOT);

    // Un soin mis de côté à la main (AiPlayerbot.CoaHealsExcluded) : il garde tous ses autres
    // effets, il cesse seulement de compter comme un soin. Le bot ne le mettra donc plus dans sa
    // trousse et n'ira jamais le chercher. C'est le seul endroit qui le tienne à l'écart des deux
    // chemins à la fois, la rotation et la trousse — et il n'est parcouru qu'au démarrage, quand
    // la table des sorts de chaque classe est construite.
    if (!depth && !sPlayerbotAIConfig.coaHealsExcluded.empty() &&
        sPlayerbotAIConfig.coaHealsExcluded.count(info->SpellName[LOCALE_enUS]))
        ability.kind &= ~(KIND_HEAL | KIND_GROUP_HEAL | KIND_HOT);

    // A spell that puts its caster in a form, or stuns or pacifies it, is never a buff to keep up, whatever
    // its other effects say. Moon Gaze (Bloodmage) is Cursed Form, a dummy and a stun on the caster for five
    // minutes: its dummy made it a buff, and bots recast it the moment it ran out and stood still, healers
    // included (jealous-sound/azerothcore-wotlk-coa#4836, #5143, #5196, #5360; still seen on 29/09).
    for (SpellEffectInfo const& effect : info->Effects)
        if (effect.IsAura() && effect.TargetA.GetTarget() == TARGET_UNIT_CASTER &&
            (effect.ApplyAuraName == SPELL_AURA_MOD_SHAPESHIFT || effect.ApplyAuraName == SPELL_AURA_MOD_STUN ||
             effect.ApplyAuraName == SPELL_AURA_MOD_PACIFY || effect.ApplyAuraName == SPELL_AURA_MOD_PACIFY_SILENCE))
            ability.kind &= ~KIND_BUFF;

    // A roll or run the caster cannot stop is no attack and no buff: Boulder Dash's damage made it one, and
    // Wildwalker bots rolled 10 s away from the boss, 20 s a fight out of reach (bench, 10/10).
    if (!depth && ForcesCasterForward(info))
        ability.kind &= ~(KIND_HOSTILE | KIND_DAMAGE | KIND_AOE | KIND_BUFF);

    // Assume Peak Posture (Guardian, 500344) is cosmetic, never runs out and ends with any movement. IsStance
    // takes it for a stance, so "coa buff" could stop the bot to cast it (1 s) and count it as the stance the
    // bot stands in. It is no stance and no buff.
    if (!depth && info->Id == 500344)
        ability.kind &= ~(KIND_STANCE | KIND_BUFF);

    // Befriend Beast (Ranger, 562301) possesses a beast for 40 s: no bot can steer it, and the beast and
    // the bot stood idle while the beast took hits (jealous-sound/azerothcore-wotlk-coa#6111). No action
    // of the kit casts such a spell.
    if (!depth && PossessesTarget(info))
        ability.kind = 0;

    // A control that deals no damage and that the first hit ends is no attack: on the enemy the group is
    // hitting it only gives that enemy time. A Time Chronomancer's attack kit cast Babify on it, which
    // also regenerates its health (jealous-sound/azerothcore-wotlk-coa#6287). It stays a control, out of
    // the attack and area attack kits.
    if (!depth && (ability.kind & KIND_CONTROL) && !(ability.kind & KIND_DAMAGE) && DamageEndsControl(info))
        ability.kind &= ~KIND_HOSTILE;

    // Nor one that makes the enemy immune or banishes it: a Templar Oathkeeper's attack kit cast Shackle the
    // Unrepentant on the enemy the group was hitting (jealous-sound/azerothcore-wotlk-coa#6724).
    if (!depth && !(ability.kind & KIND_DAMAGE) && ShieldsEnemy(info))
        ability.kind &= ~KIND_HOSTILE;
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
            kit.purgeMask |= ability.purgeMask;
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

// A Wildcard Hero has no class kit: its abilities are whatever its dice gave it. Its own kit holds the
// spells of its rolled entries, classified like a class kit, and is kept until its build changes.
std::mutex WildcardKitLock;
std::unordered_map<ObjectGuid::LowType, std::shared_ptr<ClassKit const>> WildcardKits;

std::shared_ptr<ClassKit const> BuildWildcardKit(Player const* bot)
{
    auto kit = std::make_shared<ClassKit>();
    for (auto const& [rolled, synergy] : WildcardBuildSynergies(bot))
        // The rolled rank first, then the higher ranks the bot trained: the highest it knows is cast.
        for (uint32 spellId = rolled; spellId && (spellId == rolled || bot->HasSpell(spellId));
             spellId = sSpellMgr->GetNextSpellInChain(spellId))
            if (SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId))
            {
                CoaAbility ability = { spellId, 0, 0, 0, info->GetFirstRankSpell()->Id };
                Classify(info, ability);
                ability.synergy = synergy;
                kit->abilities.push_back(ability);
                kit->kinds |= ability.kind;
            }
    return kit;
}

// The kit a bot casts from: the one of its class, or its own for a Wildcard Hero.
std::shared_ptr<ClassKit const> KitOf(Player* bot)
{
    if (IsWildcardHeroBot(bot))
    {
        ObjectGuid::LowType const guid = bot->GetGUID().GetCounter();
        {
            std::lock_guard<std::mutex> lock(WildcardKitLock);
            if (auto const found = WildcardKits.find(guid); found != WildcardKits.end())
                return found->second;
        }
        std::shared_ptr<ClassKit const> const kit = BuildWildcardKit(bot);
        std::lock_guard<std::mutex> lock(WildcardKitLock);
        return WildcardKits.try_emplace(guid, kit).first->second;
    }

    auto const& all = ClassAbilities();
    auto const found = all.find(bot->getClass());
    return found == all.end() ? nullptr : std::shared_ptr<ClassKit const>(std::shared_ptr<void>(), &found->second);
}

// Whether the bot's class has any ability of that kind: a cheap test before looking at spells.
bool ClassHas(Player* bot, uint16 kind)
{
    std::shared_ptr<ClassKit const> const kit = KitOf(bot);
    return kit && (kit->kinds & kind);
}

struct Usable
{
    SpellInfo const* info;
    uint16 kind;
    uint32 dispelMask;
    uint32 purgeMask = 0;
    uint32 synergy = 0;
};

// Active abilities the bot has reached and actually knows, for which `wanted(kind)` is true.
template <typename Filter>
std::vector<Usable> KnownAbilities(Player* bot, Filter wanted)
{
    std::vector<Usable> usable;

    std::shared_ptr<ClassKit const> const kit = KitOf(bot);
    if (!kit)
        return usable;

    // A bot can still know the lower ranks of a spell: only cast the highest one it has reached.
    // Abilities are ordered by required level, so a later rank replaces an earlier one.
    std::unordered_map<uint32, size_t> rankIndex;
    for (CoaAbility const& ability : kit->abilities)
    {
        if (!wanted(ability.kind) || ability.requiredLevel > bot->GetLevel() || !bot->HasSpell(ability.spellId))
            continue;

        // Class grants include passives (e.g. 552011 Resilient Constitution).
        SpellInfo const* info = sSpellMgr->GetSpellInfo(ability.spellId);
        if (!info || info->IsPassive())
            continue;

        auto const [itr, inserted] = rankIndex.try_emplace(ability.firstSpellId, usable.size());
        if (inserted)
            usable.push_back({ info, ability.kind, ability.dispelMask, ability.purgeMask, ability.synergy });
        else
            usable[itr->second] = { info, ability.kind, ability.dispelMask, ability.purgeMask, ability.synergy };
    }

    return usable;
}

bool IsFriendlyDispel(uint16 kind)
{
    // An area that cleanses allies and also purges or hurts the enemies in it (Resonance Rune) is
    // still a cleanse: it was taken for an attack only, and during a Hex on the healer it was the only
    // cleanse the group had (ladiabale, Zul'Farrak, 12/10).
    return (kind & KIND_DISPEL) && !(kind & KIND_CONTROL) && (!(kind & KIND_HOSTILE) || (kind & KIND_AOE));
}

// AiPlayerbot.CoaSmartDispel: a spell that also strikes enemies (a nova that burns foes and cleanses
// friends) still cleanses; where it can reach a member is left to DispelTarget.
bool IsGroupDispel(uint16 kind)
{
    if (!sPlayerbotAIConfig.coaSmartDispel)
        return IsFriendlyDispel(kind);
    return (kind & KIND_DISPEL) && !(kind & KIND_CONTROL);
}

// The spell the bot will really cast. Ascension swaps a spell for another while a proc is up
// (Malefic Wrath -> Malefic Arrow) and PlayerbotAI::CastSpell follows the swap, so the check, the
// cooldown and the bench have to bear on the replacement; the cast keeps the listed id, or the proc
// is not consumed. AiPlayerbot.CoaSpellReplacement (StevenLeclerc 9ee20729).
SpellInfo const* EffectiveSpell(Player* bot, SpellInfo const* info)
{
    if (!sPlayerbotAIConfig.coaSpellReplacement)
        return info;

    uint32 const replacement = bot->GetTemporarySpellReplacement(info->Id);
    if (replacement && replacement != info->Id)
        if (SpellInfo const* swapped = sSpellMgr->GetSpellInfo(replacement))
            return swapped;

    return info;
}

bool IsBenched(std::unordered_map<uint32, time_t> const& benched, uint32 spellId, time_t now)
{
    auto const bench = benched.find(spellId);
    return bench != benched.end() && bench->second > now;
}

// Whether the bot knows an ability for which `wanted(kind)` is true and that is off cooldown:
// actions only run when they have something to cast, which keeps the usage counters honest.
template <typename Filter>
bool HasReadyAbility(PlayerbotAI* botAI, Player* bot, Filter wanted)
{
    auto const& benched = static_cast<CoaAiObjectContext*>(botAI->GetAiObjectContext())->benchedSpells;
    time_t const now = time(nullptr);
    // a silenced bot is not ready to cast what a silence stops (a Runemaster's dispels failed 478 times in Sonic Burst)
    bool const silenced = bot->HasUnitFlag(UNIT_FLAG_SILENCED);

    for (Usable const& spell : KnownAbilities(bot, wanted))
    {
        SpellInfo const* const info = EffectiveSpell(bot, spell.info);
        if (silenced && info->PreventionType == SPELL_PREVENTION_TYPE_SILENCE)
            continue;
        if (!bot->HasSpellCooldown(info->Id) && !IsBenched(benched, spell.info->Id, now) &&
            (info == spell.info || !IsBenched(benched, info->Id, now)))
            return true;
    }

    return false;
}

// Abilities worth using in the damage rotation.
// Damage of its own on the hit, not only through an aura it leaves.
bool HitsDirectly(SpellInfo const* info)
{
    for (SpellEffectInfo const& effect : info->Effects)
        if (effect.Effect == SPELL_EFFECT_SCHOOL_DAMAGE || effect.Effect == SPELL_EFFECT_WEAPON_DAMAGE ||
            effect.Effect == SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL || effect.Effect == SPELL_EFFECT_NORMALIZED_WEAPON_DMG ||
            effect.Effect == SPELL_EFFECT_WEAPON_PERCENT_DAMAGE || effect.Effect == SPELL_EFFECT_HEALTH_LEECH)
            return true;
    return false;
}

// An attack whose point is the aura it keeps on the target: a damage over time or a debuff. A strike
// with a side effect (the slow of Frostbolt) is not one.
bool KeepsAuraOnTarget(SpellInfo const* info)
{
    if (HitsDirectly(info))
        return false;
    for (SpellEffectInfo const& effect : info->Effects)
        if (effect.IsAura())
            return true;
    return false;
}

// 2: a cooldown longer than the global one; 1: an aura kept on the target; 0: everything else.
uint8 AttackTier(SpellInfo const* info)
{
    if (std::max(info->RecoveryTime, info->CategoryRecoveryTime) > 1500)
        return 2;
    return KeepsAuraOnTarget(info) ? 1 : 0;
}

// The bot's own damage over time or debuff on the target with more than 3 seconds and all its stacks
// left: casting it again only renews it.
bool AuraStillRunning(Player* bot, SpellInfo const* info, Unit* target)
{
    if (!KeepsAuraOnTarget(info))
        return false;
    Aura const* aura = target->GetAura(info->Id, bot->GetGUID());
    return aura && (aura->GetDuration() < 0 || aura->GetDuration() > 3000) &&
           (info->StackAmount <= 1 || aura->GetStackAmount() >= info->StackAmount);
}

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
    // A cleanse that also purges an enemy deals no damage: Resonance Rune, a 1.5 s cast on a 60 s cooldown, went
    // 52 times as an attack against 11 cleanses.
    if ((kind & KIND_DISPEL) && !(kind & KIND_DAMAGE))
        return false;

    // A taunt pulls aggro off the tank, with or without damage: only tanks use it. A Witch Hunter
    // dealing damage kept taunting the mobs off a player tank (Dogsmeller, 07/10).
    if (kind & KIND_TAUNT)
        return tank;

    if (kind & KIND_DAMAGE)
        return true;

    // Interrupts are kept for enemy casts.

    return !(kind & KIND_INTERRUPT);
}

// Whether another tank holds this enemy. As for "has aggro", only the main tank set on the group takes
// it from them: otherwise two tanks taunt back and forth what the other holds, and a bot tank pulls the
// boss off a player tank (the same fault fixed in three forks: sksmith261, ShatteredDawn, pengjinfei).
bool HeldByOtherTank(Player* bot, Unit* enemy)
{
    Unit* victim = enemy->GetVictim();
    Player* holder = victim ? victim->ToPlayer() : nullptr;
    return holder && holder != bot && PlayerbotAI::IsTank(holder) && !PlayerbotAI::IsExplicitMainTank(bot);
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
    // The spell prepare() will see, replacement included: its cost and range, not the base spell's.
    info = EffectiveSpell(bot, info);

    // The core lets a boss be charmed; a bot never tries (#4835).
    if (TakesOverBoss(info, target))
        return SPELL_FAILED_BAD_TARGETS;

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

// Whether the bot can use this spell in the state it is in: Spell::CheckCasterAuras' rules, read before asking.
bool CanActNow(Player* bot, SpellInfo const* info)
{
    if (bot->HasUnitState(UNIT_STATE_CHARMED) || bot->IsCharmed())
        return false;
    if (bot->HasUnitState(UNIT_STATE_STUNNED) && !info->HasAttribute(SPELL_ATTR5_ALLOW_WHILE_STUNNED))
        return false;
    if (bot->HasUnitState(UNIT_STATE_FLEEING) && !info->HasAttribute(SPELL_ATTR5_ALLOW_WHILE_FLEEING))
        return false;
    if (bot->HasUnitState(UNIT_STATE_CONFUSED) && !info->HasAttribute(SPELL_ATTR5_ALLOW_WHILE_CONFUSED))
        return false;
    if (bot->HasUnitFlag(UNIT_FLAG_SILENCED) && info->PreventionType & SPELL_PREVENTION_TYPE_SILENCE)
        return false;
    return true;
}

constexpr uint16 FAILURE_REFUSED = 1000;  // passed the strict check, refused by PlayerbotAI::CastSpell
constexpr uint16 FAILURE_NOTHING = 1001;  // nothing left to cast (e.g. every heal over time already on the target)
constexpr uint16 FAILURE_MOVING = 1002;   // cast time while moving: the bot stops and casts on a later tick
constexpr uint16 FAILURE_SITTING = 1003;  // refused while sitting (eating, drinking): the bot stands up first
constexpr uint16 FAILURE_CASTING = 1004;  // refused while still casting a heal or buff
constexpr uint16 SKIPPED_COOLDOWN = 2000;  // not tried: on cooldown (group fight log only)
constexpr uint16 SKIPPED_BENCHED = 2001;   // not tried: set aside after an earlier failure (group fight log only)

// Whether a cast of this usage kind drops the attack in progress: an attack never cuts off another.
bool PreemptsAttackCast(uint8 usage);

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
// with what costs nothing, where alone it only kept AiPlayerbot.CoaHealerManaReserve. At 85 it was
// saving nearly all fight long and chose its cheapest heals while members died (28/09).
constexpr uint32 GroupHealerManaReserve = 60;

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

// The player the group plays for: the bot's master, or, for a bot of the dungeon finder (it has none),
// the real player of its group on its instance. The tank of a dungeon finder group never pulled.
Player* GroupPlayer(PlayerbotAI* botAI, Player* bot)
{
    Player* master = botAI->GetMaster();
    if (master && !GET_PLAYERBOT_AI(master))
        return master;
    if (Group* group = bot->GetGroup())
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            if (Player* member = ref->GetSource())
                if (member != bot && !GET_PLAYERBOT_AI(member) && OnSameInstance(bot, member))
                    return member;
    return nullptr;
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

// A heal over time and nothing else: all it does is its aura, so cast again on a target carrying it
// from the same caster it only renews it. One with a direct part as well (Shadra's Prayer, Regrowth,
// Potion Toss) still heals at once and stays a direct heal.
bool IsPureHot(Usable const& spell)
{
    return (spell.kind & KIND_HOT) && !spell.info->HasEffect(SPELL_EFFECT_HEAL) &&
           !spell.info->HasEffect(SPELL_EFFECT_HEAL_PCT) && !spell.info->HasEffect(SPELL_EFFECT_HEAL_MAX_HEALTH);
}

// Whether `target` already carries every one of `hots` from `caster`: no heal over time left to put on it.
bool CarriesAllHots(Unit* target, std::vector<Usable> const& hots, ObjectGuid caster)
{
    if (hots.empty())
        return false;
    for (Usable const& spell : hots)
        if (!target->HasAura(spell.info->Id, caster))
            return false;
    return true;
}

std::vector<Usable> KnownHots(Player* bot)
{
    return KnownAbilities(bot, [](uint16 kind) { return (kind & KIND_HOT) && !(kind & (KIND_CONTROL | KIND_HOSTILE)); });
}

// The member to heal, of those under `below` percent: under the critical threshold before anyone
// else, then the lowest health with the tank counted 15 points lower, as the one taking the hits.
// One another healer is already healing is left to it unless dropping; for a heal over time, one
// already carrying a heal over time too, unless under the medium line, and never one already carrying
// every heal over time of this healer: there it had nothing left to cast and failed tick after tick,
// and a tank without one was not picked while that member stayed the lowest (02/10: 922 of 1715 tries
// of the heal over time cast nothing).
Unit* SmartHealTarget(Player* bot, float below, bool overTime = false)
{
    std::vector<Usable> const hots = overTime ? KnownHots(bot) : std::vector<Usable>();
    ObjectGuid const caster = bot->GetGUID();

    Group* group = bot->GetGroup();
    if (!group)
        return bot->GetHealthPct() < below && !(overTime && CarriesAllHots(bot, hots, caster)) ? bot : nullptr;

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
        if (overTime && CarriesAllHots(member, hots, caster))
            continue;

        float score = health;
        if (PlayerbotAI::IsTank(member))
            score -= 15.0f;
        // A battleground flag carrier before the tank: the game hangs on it reaching its base alive.
        if (member->HasAura(23333) || member->HasAura(23335) || member->HasAura(34976))
            score -= 20.0f;
        if (health < sPlayerbotAIConfig.criticalHealth)
            score -= 40.0f;

        if (score < bestScore)
        {
            best = member;
            bestScore = score;
        }
    }

    if (sPlayerbotAIConfig.coaHealSelfFirst && best != bot && GetCoaRole(bot) == CoaRole::Heal &&
        bot->GetHealthPct() < std::min<float>(below, sPlayerbotAIConfig.mediumHealth) &&
        (!best || best->GetHealthPct() > sPlayerbotAIConfig.lowHealth) && !(overTime && CarriesAllHots(bot, hots, caster)))
        return bot;

    return best;
}

// Rough healing of one cast before bonuses: direct heals, plus every tick of a heal over time.
// Enough to tell a small heal from a large one; CoA's scripted heals may read as 0.
float HealAmount(Player* bot, SpellInfo const* info, Unit* target)
{
    if (!info || !target)
        return 0.0f;

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

// The health the group is missing around the healer: what an area heal has to fill.
float GroupMissingHealth(Player* bot)
{
    Group* group = bot->GetGroup();
    if (!group)
        return float(bot->GetMaxHealth() - bot->GetHealth());

    float missing = 0.0f;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        if (Player* member = ref->GetSource())
            if (member->IsAlive() && member->IsInWorld() && bot->GetDistance(member) < 30.0f)
                missing += float(member->GetMaxHealth() - member->GetHealth());
    return missing;
}

// Heals ordered by what they would really heal against the health that is missing: the largest
// that still fits the gap first, and none at all when even the smallest would mostly land on full
// health.
//
// Ce que rend chaque soin est calculé UNE fois, avant le tri. Le calculer dans le comparateur, comme
// c'était écrit, fait dépendre le résultat du nombre d'appels : si deux appels ne rendent pas
// exactement la même valeur, l'ordre cesse d'être strict, et le tri de MSVC sort alors du tableau et
// lit de la mémoire quelconque. Onze plantages dans `HealAmount` le 23/09, un par heure.
void OrderByFit(Player* bot, Unit* target, std::vector<Usable>& spells, float room, float fits, float waste)
{
    if (spells.empty() || !target)
        return;

    std::vector<std::pair<float, Usable>> scored;
    scored.reserve(spells.size());
    for (Usable const& spell : spells)
        scored.emplace_back(HealAmount(bot, spell.info, target), spell);

    std::stable_sort(scored.begin(), scored.end(), [room, fits](auto const& a, auto const& b)
    {
        bool const aFits = a.first <= room * fits;
        bool const bFits = b.first <= room * fits;
        if (aFits != bFits)
            return aFits;
        return aFits ? a.first > b.first : a.first < b.first;
    });

    if (scored.front().first > room * waste)
    {
        spells.clear();
        return;
    }
    for (size_t i = 0; i < scored.size(); ++i)
        spells[i] = scored[i].second;
}

// Heals over time: all their ticks against the health that is missing, plus what a tank being hit
// is about to lose.
void OrderByHotFit(Player* bot, Unit* target, std::vector<Usable>& spells)
{
    if (!target)
        return;

    // Santé manquante sans soustraction non signée : un bot porté au-dessus de son maximum par une
    // aura donnerait un trou immense au lieu de zéro, et tous les soins « tiendraient » dedans.
    uint32 const maxHealth = target->GetMaxHealth();
    uint32 const health = target->GetHealth();
    float room = float(maxHealth > health ? maxHealth - health : 0);
    if (target == GroupTank(bot) && !target->getAttackers().empty())
        room += float(maxHealth) * 0.25f;  // the hits still to come

    OrderByFit(bot, target, spells, room, 1.5f, 3.0f);
}

// Area heals: the biggest that still fits the health the group is missing. Even the smallest one
// pouring more than two thirds into full health: keep the mana.
void OrderByGroupFit(Player* bot, Unit* target, std::vector<Usable>& spells)
{
    OrderByFit(bot, target, spells, GroupMissingHealth(bot), 1.2f, 3.0f);
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
        // Stunned or in stasis: asking again every tick changed nothing (3,848 tries in a row for a
        // Bloodmage, 28/09).
        case SPELL_FAILED_STUNNED:
            return true;
        default:
            return false;
    }
}
void RecordFailure(uint8 kind, uint32 spellId, uint16 reason);

// Casts the first ability of the list that passes the strict check on the target. Returns it,
// or nullptr when none went off. With a usage kind, why each ability failed is counted.

// How long the creature a spell summons stays, looking into the spells it triggers: 0 when the
// spell summons nothing. Totems and wards are summons without a cooldown, and a bot recast them
// on every global cooldown while the first one was still up (Cultist Tentacle of Yogg-Saron,
// Witch Doctor Healing Ward, jealous-sound/azerothcore-wotlk-coa#5739, #5605).
time_t SummonSeconds(SpellInfo const* info, uint8 depth = 0)
{
    for (SpellEffectInfo const& effect : info->Effects)
    {
        if (effect.Effect == SPELL_EFFECT_SUMMON)
        {
            int32 const duration = info->GetMaxDuration();
            return duration > 0 ? std::min<time_t>(duration / IN_MILLISECONDS, 120) : 30;
        }
        if (effect.TriggerSpell && depth < 2)
            if (SpellInfo const* triggered = sSpellMgr->GetSpellInfo(effect.TriggerSpell))
                if (time_t const seconds = SummonSeconds(triggered, depth + 1))
                    return seconds;
    }
    return 0;
}

// How far an attack centred on its caster reaches: a cleave, a cone or a spin that hits
// around the bot, whatever unit it is aimed at. 0 when the spell reaches its target instead.
float CasterCentredReach(Player* bot, SpellInfo const* info)
{
    float reach = 0.0f;
    for (SpellEffectInfo const& effect : info->Effects)
    {
        if (!effect.IsEffect() || (!IsEnemyTarget(effect.TargetA.GetTarget()) && !IsEnemyTarget(effect.TargetB.GetTarget())))
            continue;

        uint32 const a = effect.TargetA.GetTarget();
        bool const centred = a == TARGET_SRC_CASTER || a == TARGET_UNIT_CONE_ENEMY_24 || a == TARGET_UNIT_CONE_ENEMY_104 ||
                             a == TARGET_UNIT_CONE_ENEMY_54;
        if (!centred)
            return 0.0f;
        reach = std::max(reach, effect.CalcRadius(bot));
    }
    return reach;
}

// The longest channel of its own a bot lets run to its end with AiPlayerbot.CoaKeepChannels: an
// endless or very long one (a hidden aura channel, a drain held for minutes) is not waited for.
constexpr int32 KeptChannelMaxMs = 15 * IN_MILLISECONDS;

// The bot's own channel when it is one to let run to its end, nullptr otherwise. Only an attack channel,
// one that strikes enemies and heals no one (Classify): Stormflow, Aortic Assault, Twilight Frenzy, a
// tank's Carve. SpellInfo::IsPositive is no guide: it takes a channel whose damage goes through a spell
// it triggers on enemies for a helpful one, and Testament of Fortitude, a tank's defensive, for a harmful
// one. Neither by a healer nor by a bot at critical health: while a channel runs, "reach party member to
// heal", "flee" and the other moves wait, and a heal on a member out of reach fails its range check, so a
// Witch Doctor holding Mojo Beam stood still while that member died. Those channels end with the next
// spell, as in 1.6.
SpellInfo const* HeldChannel(Player* bot)
{
    if (!sPlayerbotAIConfig.coaKeepChannels)
        return nullptr;
    Spell const* channel = bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
    if (!channel || channel->getState() != SPELL_STATE_CASTING)
        return nullptr;
    SpellInfo const* const info = channel->GetSpellInfo();
    int32 const duration = info->GetMaxDuration();
    if (duration <= 0 || duration > KeptChannelMaxMs || GetCoaRole(bot) == CoaRole::Heal ||
        bot->GetHealthPct() < float(sPlayerbotAIConfig.criticalHealth))
        return nullptr;
    CoaAbility channelKind{};
    Classify(info, channelKind);
    if (!(channelKind.kind & (KIND_DAMAGE | KIND_HOSTILE)) || (channelKind.kind & KIND_HEAL))
        return nullptr;
    return info;
}

// Whether casting this spell now would break the bot's own channel, one it should let run (HeldChannel):
// the core breaks a channel for any spell but those Unit::CanCastDuringChannel lets through (Brine and
// Electrocute during a Stormbringer's Stormflow) or a channel that allows actions, and a new channel
// always breaks the current one. The attack loop took the next spell as soon as the global cooldown
// ended - 250 ms after Stormflow began, as its own global cooldown is that short - and cut the 8 s beam
// after a tick or two (players' report of 02/10, Maelstrom).
bool BreaksOwnChannel(Player* bot, SpellInfo const* info)
{
    SpellInfo const* const channel = HeldChannel(bot);
    return channel &&
           (info->IsChanneled() || (!channel->IsActionAllowedChannel() && !bot->CanCastDuringChannel(info)));
}

// Drops the attacks that would break the bot's own channel (see BreaksOwnChannel): an attack or an
// area attack waits for it, in the attack loop of AiPlayerbot.CoaAttackLoop as in that of 1.6.
void DropChannelBreakers(Player* bot, std::vector<Usable>& spells)
{
    if (!HeldChannel(bot))
        return;
    spells.erase(std::remove_if(spells.begin(), spells.end(), [bot](Usable const& spell)
        { return BreaksOwnChannel(bot, EffectiveSpell(bot, spell.info)); }),
        spells.end());
}

SpellInfo const* CastFirst(PlayerbotAI* botAI, Player* bot, std::vector<Usable> const& spells, Unit* target,
                           uint8 usage = 255)
{
    time_t const now = time(nullptr);
    auto& benched = static_cast<CoaAiObjectContext*>(botAI->GetAiObjectContext())->benchedSpells;
    // Heals are followed spell by spell in a measured group fight, skipped ones included.
    // USAGE_HEAL, USAGE_GROUP_HEAL and USAGE_HOT, whose enum comes further down.
    bool const healing = usage == 2 || usage == 3 || usage == 4;
    // USAGE_ATTACK: the attack loop runs again on the next tick, and a proc, a target aura or a
    // few points of rage come back sooner than a bench ends. As in 1.6, an attack turned down by
    // the check is only skipped, unless AiPlayerbot.CoaAttackCheckBench.
    bool const retryAttack = usage == 0 && !sPlayerbotAIConfig.coaAttackCheckBench;
    auto note = [bot, healing](uint32 spellId, uint16 outcome)
    {
        if (healing)
            CoaTelemetryNoteHeal(bot, spellId, outcome);
    };

    for (Usable const& spell : spells)
    {
        // What the bot will really cast (see EffectiveSpell): cooldown, cast time, bench and the
        // failure log go by the replacement, the cast itself by the listed id.
        SpellInfo const* const info = EffectiveSpell(bot, spell.info);

        // Feared, stunned, confused or silenced, the bot only looks at what it may use as it is; the rest is neither
        // tried nor set aside. 310 heals were tried under Hellmaw's fear (ladiabale, 16/10).
        if (!CanActNow(bot, info))
            continue;

        // Thrown back at the bot for as long as the reflect lasts (seconds): skipped, never set aside.
        if (target && target != bot && PlayerbotAI::WouldBeReflected(bot, info, target))
        {
            note(spell.info->Id, SKIPPED_BENCHED);
            continue;
        }

        // A spell on cooldown would only fail with SPELL_FAILED_NOT_READY.
        if (bot->HasSpellCooldown(info->Id))
        {
            note(spell.info->Id, SKIPPED_COOLDOWN);
            continue;
        }

        // The global cooldown blocks every spell alike: try again on a later tick.
        if (bot->GetGlobalCooldownMgr().HasGlobalCooldown(info))
            return nullptr;

        // The bench is posted under the id that was checked: a failure of a short-lived replacement
        // never sidelines the listed spell. Both ids are read, spent entries dropped.
        bool stillBenched = false;
        for (uint32 const benchedId : { spell.info->Id, info->Id })
        {
            auto const bench = benched.find(benchedId);
            if (bench == benched.end())
                continue;
            if (bench->second > now)
                stillBenched = true;
            else
                benched.erase(bench);
            if (info == spell.info)
                break;
        }
        if (stillBenched)
        {
            note(spell.info->Id, SKIPPED_BENCHED);
            continue;
        }

        // An attack that only puts an aura on the caster, already worn: Searing Quiver, a lasting self-aura, was
        // recast 282 times in one run while it was up (a quarter of that Ranger's attack casts).
        if (usage == 0 && bot->HasAura(info->Id))
        {
            bool selfOnly = false;
            for (SpellEffectInfo const& effect : info->Effects)
            {
                if (!effect.IsEffect())
                    continue;
                selfOnly = effect.IsAura() && effect.TargetA.GetTarget() == TARGET_UNIT_CASTER;
                if (!selfOnly)
                    break;
            }
            if (selfOnly)
                continue;
        }

        // A melee cleave or spin hits around the bot, not at its target: cast from afar it only
        // cost a global cooldown and resources (KoX Meatsaw, Guardian Broad Sweep on the pull,
        // jealous-sound/azerothcore-wotlk-coa#5500, #5453).
        if (target && target != bot)
            if (float const reach = CasterCentredReach(bot, info))
                if (!bot->IsWithinDistInMap(target, reach))
                {
                    note(spell.info->Id, SKIPPED_BENCHED);
                    continue;
                }

        // AiPlayerbot.CoaMeleeNoRangedHardcast: a melee specialization in a fight does not stop short of its
        // target to cast an attack with a cast time or a channel: it closes in ("reach melee") and uses
        // only its instants on the way. A rooted bot, or a target it cannot walk to (far above or below),
        // still gets the cast.
        if (sPlayerbotAIConfig.coaMeleeNoRangedHardcast && (usage == 0 || usage == 1) && target && target != bot &&
            bot->IsInCombat() && PlayerbotAI::IsMelee(bot) && !PlayerbotAI::IsHeal(bot) && !bot->IsRooted() &&
            std::abs(target->GetPositionZ() - bot->GetPositionZ()) < 8.0f &&
            !bot->IsWithinMeleeRange(target, bot->GetMeleeRange(target) + 3.0f) &&
            (info->CalcCastTime(bot) > 0 || info->IsChanneled()))
            continue;

        SpellCastResult const check = StrictCheck(bot, info, target);
        if (check == SPELL_CAST_OK)
        {
            // PlayerbotAI::CastSpell refuses a spell with a cast time while the bot moves: stop
            // now and cast it on a later tick, once standing still.
            if (bot->isMoving() && info->CalcCastTime(bot))
            {
                // Out of combat nothing is urgent: stopping the follow for a top-up heal that the next tick
                // might not cast made bots stutter behind their master (jealous-sound/azerothcore-wotlk-coa#5595).
                if (bot->IsInCombat())
                    bot->StopMoving();
                if (usage != 255)
                    RecordFailure(usage, info->Id, FAILURE_MOVING);
                note(spell.info->Id, FAILURE_MOVING);
                continue;
            }

            if (PreemptsAttackCast(usage))
                DropAttackCast(bot);

            bool const sitting = !bot->IsStandState();
            bool const casting = bot->IsNonMeleeSpellCast(false, true, true);
            if (botAI->CastSpell(spell.info->Id, target))
            {
                note(spell.info->Id, 0);
                if (time_t const seconds = SummonSeconds(info))
                    benched[spell.info->Id] = now + seconds;
                return spell.info;
            }

            // Refused for a reason the check cannot see (CoA spell scripts check their own
            // resources when the cast is prepared): leave it aside briefly so the next ability
            // of the list gets its turn instead of this one failing every tick.
            if (!sitting && !casting)
                benched[info->Id] = now + RefusedBenchSeconds;

            if (usage != 255)
                RecordFailure(usage, info->Id, sitting ? FAILURE_SITTING : casting ? FAILURE_CASTING : FAILURE_REFUSED);
            note(spell.info->Id, sitting ? FAILURE_SITTING : casting ? FAILURE_CASTING : FAILURE_REFUSED);
            continue;
        }
        // A heal that cannot be cast in a form (most Venomancer heals) while the healer stands in
        // one: it leaves the form and heals on the next tick, instead of setting its heals aside.
        else if (check == SPELL_FAILED_NOT_SHAPESHIFT && healing && bot->HasAuraType(SPELL_AURA_MOD_SHAPESHIFT))
            bot->RemoveAurasByType(SPELL_AURA_MOD_SHAPESHIFT);
        else if (check == SPELL_FAILED_CASTER_AURASTATE && info->CasterAuraSpell)
        {
            // Waiting on its marker (see CoaHealAction::AddPrerequisites): not set aside.
        }
        else if (IsLastingFailure(check) && (!retryAttack || check == SPELL_FAILED_STUNNED))
            benched[info->Id] = now + SpellBenchSeconds;
        // Out of mana, energy or rage: asking again on the very next tick changes nothing, and
        // with the spell set aside the action reports itself useless, so the bot does something
        // it can afford instead of spending its ticks being turned down.
        else if (check == SPELL_FAILED_NO_POWER && !retryAttack)
            benched[info->Id] = now + NoPowerBenchSeconds;

        if (usage != 255)
            RecordFailure(usage, info->Id, uint16(check));
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

bool PreemptsAttackCast(uint8 usage)
{
    return usage != 255 && usage != USAGE_ATTACK && usage != USAGE_AOE;
}

// Usage kind passed to CastFirst by the area attack, defensive and interrupt actions: none in the
// 1.6 loop, their own with AiPlayerbot.CoaAttackLoop, so that they drop an attack cast and log why
// they fail.
uint8 LoopUsage(UsageKind kind)
{
    return sPlayerbotAIConfig.coaAttackLoop ? uint8(kind) : uint8(255);
}

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

// What became of the interruptible enemy casts grouped bots saw (see InterruptClaims below), with
// AiPlayerbot.CoaInterruptCoordination on or off, so that both can be compared on the same runs.
struct InterruptCounters
{
    std::atomic<uint64> seen{ 0 };        // enemy casts a grouped bot with an interrupt ready saw
    std::atomic<uint64> seenDanger{ 0 };  // of which heals, crowd control, summons, channels, long casts
    std::atomic<uint64> kicked{ 0 };      // enemy casts at least one kick went to
    std::atomic<uint64> kickedDanger{ 0 };
    std::atomic<uint64> kicks{ 0 };       // kicks cast by grouped bots
    std::atomic<uint64> wasted{ 0 };      // kicks at a cast another bot had already kicked
    std::atomic<uint64> waited{ 0 };      // times a bot let the chosen one kick
    std::atomic<uint64> takenOver{ 0 };   // the chosen bot let the hand-over pass and another kicked
    std::atomic<uint64> heldBack{ 0 };    // routine casts let through to keep the last kick in reach
};

InterruptCounters InterruptStats;
bool CoordinateInterrupts() { return sPlayerbotAIConfig.coaInterruptCoordination; }

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

    InterruptCounters const& kicks = InterruptStats;
    LOG_INFO("playerbots.coa", "coa interrupt coordination {}: enemy casts seen {} ({} dangerous), kicked {} ({} dangerous), "
             "kicks {}, on a cast already kicked {}, waited for another bot {}, taken over {}, routine casts let through {}",
             CoordinateInterrupts() ? "on" : "off", kicks.seen.load(), kicks.seenDanger.load(), kicks.kicked.load(),
             kicks.kickedDanger.load(), kicks.kicks.load(), kicks.wasted.load(), kicks.waited.load(),
             kicks.takenOver.load(), kicks.heldBack.load());

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
    for (UsageKind kind : { USAGE_HEAL, USAGE_GROUP_HEAL, USAGE_HOT, USAGE_TAUNT, USAGE_DISPEL,
                            USAGE_ATTACK, USAGE_AOE, USAGE_DEFENSIVE, USAGE_INTERRUPT })
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
        // An aura hidden from the player is a marker, not a buff it chose: Draconic Form Hidden (704262)
        // shares its category with the Draconic Aspect form (802117), and the Pyromancers who carried it
        // never took their form (mightyrathalos, 14/10).
        if (auraInfo && auraInfo->GetCategory() == category && !auraInfo->HasAttribute(SPELL_ATTR0_DO_NOT_DISPLAY))
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

// Whether a dispellable harmful aura of the unit takes control of it (stun, fear, polymorph, silence...).
bool HasDispellableControl(Unit* unit, uint32 mask)
{
    for (auto const& [auraId, application] : unit->GetAppliedAuras())
    {
        if (application->IsPositive())
            continue;

        SpellInfo const* auraInfo = application->GetBase()->GetSpellInfo();
        if (!auraInfo->Dispel || !(mask & (1 << auraInfo->Dispel)))
            continue;

        for (SpellEffectInfo const& effect : auraInfo->Effects)
            if (IsAuraEffect(effect) && IsControlAura(effect))
                return true;
    }

    return false;
}

// The unit a dispel has to be cast on to cleanse `member`, or nullptr when it cannot reach him.
// Without AiPlayerbot.CoaSmartDispel: the bot itself, or the member for a spell cast on an ally.
// With it, also a spell put on the ground (PlayerbotAI::CastSpell lays it at the unit given) and a
// cleanse around the caster that reaches the member.
Unit* DispelTarget(Player* bot, Usable const& spell, Player* member)
{
    if (member == bot)
        return bot;
    if (spell.kind & KIND_ALLY_CAST)
        return member;
    if (!sPlayerbotAIConfig.coaSmartDispel)
        return nullptr;

    if (spell.info->Targets & TARGET_FLAG_DEST_LOCATION)
        return member;

    for (SpellEffectInfo const& effect : spell.info->Effects)
    {
        if (effect.Effect != SPELL_EFFECT_DISPEL)
            continue;

        switch (effect.TargetA.GetTarget())
        {
            case TARGET_UNIT_CASTER_AREA_PARTY: case TARGET_UNIT_SRC_AREA_ALLY: case TARGET_UNIT_SRC_AREA_PARTY:
            case TARGET_UNIT_CASTER_AREA_RAID:
                if (bot->IsWithinDistInMap(member, effect.CalcRadius(bot)))
                    return bot;
                break;
            default:
                break;
        }
    }

    return nullptr;
}

// Another bot of the group is already casting a cleanse on the member: one dispel per member, not
// every dispeller of the group on the same one (idea from ArkosLUL bbb7e02d839).
bool DispelClaimed(Player* bot, Player* member)
{
    Group* const group = bot->GetGroup();
    if (!group)
        return false;

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* const other = ref->GetSource();
        if (!other || other == bot || !other->IsInWorld() || other->GetMapId() != bot->GetMapId())
            continue;

        Spell* const cast = other->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (cast && cast->m_targets.GetUnitTargetGUID() == member->GetGUID() &&
            cast->GetSpellInfo()->HasEffect(SPELL_EFFECT_DISPEL))
            return true;
    }

    return false;
}

// The cast of the unit an interrupt would stop, if any.
Spell* InterruptibleSpell(Unit* unit)
{
    if (Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL))
        if (spell->getState() == SPELL_STATE_PREPARING &&
            (spell->GetSpellInfo()->InterruptFlags & SPELL_INTERRUPT_FLAG_INTERRUPT))
            return spell;

    if (Spell* spell = unit->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
        if (spell->getState() == SPELL_STATE_CASTING &&
            (spell->GetSpellInfo()->ChannelInterruptFlags & CHANNEL_INTERRUPT_FLAG_INTERRUPT))
            return spell;

    return nullptr;
}

bool IsInterruptibleCast(Unit* unit) { return InterruptibleSpell(unit) != nullptr; }

/*
 * Interrupt coordination (AiPlayerbot.CoaInterruptCoordination).
 *
 * Every bot of a group kicked the first interruptible cast it saw, the moment its own tick came.
 * An instant kick lands at once and the others then see no cast, but a kick that travels (Shield
 * of Denial, Chainwhip, Burrow Bolt, Toxic Dart, Hammer of the Law) leaves the cast running for
 * a few hundred milliseconds, and every other bot ready at that moment spends its kick on it too.
 * And whoever kicked first was whoever ticked first: the tank as often as anyone, on a two second
 * Frostbolt as readily as on a heal, so that nobody had a kick left for the heal that followed.
 * 6 out of 7 tries also came to nothing (17 182 kicks for 114 381 tries on 01/10), mostly
 * SPELL_FAILED_OUT_OF_RANGE: a five yard kick tried on casters up to 30 yards away.
 *
 * With coordination on, the first grouped bot that sees a cast chooses who kicks it among the
 * bots of its group on the same map instance that are fighting and have a kick ready within
 * reach: a damage dealer before a healer before the tank, and among them the one that kicked
 * longest ago. The others leave that cast alone until the kick has landed, or until the chosen
 * bot let its hand-over time pass (stunned, busy, moved off) and one of them takes it over. In a
 * dungeon a routine cast is let through when only one kick is left in reach, kept for the heal,
 * crowd control or long cast that follows. A bot without a group is left as it was; a real
 * player is never counted on, as nothing tells what they mean to kick.
 *
 * Claims are kept by map instance and caster: creature guids are only unique within a map. A
 * group can spread over several maps, each updated by its own thread, hence the lock; it is only
 * taken while an interruptible enemy cast is in view of a grouped bot that has a kick ready.
 */
constexpr uint32 InterruptHandOverMs = 600;    // how long the chosen bot has to kick before another may
constexpr uint32 InterruptLandMarginMs = 300;  // after a kick should have landed: the cast is then over or missed
constexpr int32 DangerousCastMs = 2500;        // a longer cast is taken for a big hit

struct InterruptClaim
{
    uint32 spellId = 0;   // the enemy cast
    ObjectGuid bot;       // the bot that is to kick it, or that did; empty when it is let through
    uint32 handOver = 0;  // getMSTime() after which another bot may take it
    uint32 until = 0;     // getMSTime() after which the claim is dropped
    bool danger = false;
    bool kicked = false;
};

using CasterKey = std::tuple<uint32, uint32, uint64>;  // map, instance, caster guid
std::mutex InterruptClaimsLock;
std::map<CasterKey, InterruptClaim> InterruptClaims;

// Whether `when` (a getMSTime() value) is reached, through the wrap of the millisecond clock.
bool Passed(uint32 now, uint32 when) { return int32(now - when) >= 0; }

CasterKey KeyOf(Unit* caster)
{
    return { caster->GetMapId(), caster->GetInstanceId(), caster->GetGUID().GetRawValue() };
}

// Counts a claim that is dropped: one enemy cast seen, and whether it was kicked.
void Retire(InterruptClaim const& claim)
{
    ++InterruptStats.seen;
    InterruptStats.seenDanger += claim.danger;
    InterruptStats.kicked += claim.kicked;
    InterruptStats.kickedDanger += claim.kicked && claim.danger;
}

// Under InterruptClaimsLock. A handful of entries at most: the casts in progress right now.
void DropSpentClaims(uint32 now)
{
    for (auto itr = InterruptClaims.begin(); itr != InterruptClaims.end();)
        if (Passed(now, itr->second.until))
        {
            Retire(itr->second);
            itr = InterruptClaims.erase(itr);
        }
        else
            ++itr;
}

// Whether letting this cast through hurts the group more than an ordinary hit: a heal, crowd
// control, a summon, a shield, a channel, or a long cast that is likely a big hit.
bool IsDangerousCast(Spell* cast)
{
    SpellInfo const* info = cast->GetSpellInfo();
    if (info->IsChanneled() || cast->GetCastTime() >= DangerousCastMs)
        return true;

    for (SpellEffectInfo const& effect : info->Effects)
    {
        switch (effect.Effect)
        {
            case SPELL_EFFECT_HEAL: case SPELL_EFFECT_HEAL_PCT: case SPELL_EFFECT_HEAL_MAX_HEALTH:
            case SPELL_EFFECT_SUMMON:
                return true;
            default:
                break;
        }
        if (!effect.IsAura())
            continue;
        switch (effect.ApplyAuraName)
        {
            case SPELL_AURA_PERIODIC_HEAL: case SPELL_AURA_MOD_STUN: case SPELL_AURA_MOD_FEAR:
            case SPELL_AURA_MOD_CONFUSE: case SPELL_AURA_MOD_CHARM: case SPELL_AURA_MOD_POSSESS:
            case SPELL_AURA_MOD_SILENCE: case SPELL_AURA_MOD_PACIFY_SILENCE: case SPELL_AURA_MOD_ROOT:
            case SPELL_AURA_SCHOOL_ABSORB: case SPELL_AURA_SCHOOL_IMMUNITY:
                return true;
            default:
                break;
        }
    }
    return false;
}

// Whether this kick reaches the caster from where the bot stands. A kick with no range of its
// own hits around the bot (Gust of Wind, Ghastly Screech); five yards or less is a melee kick.
bool KickReaches(Player* bot, SpellInfo const* kick, Unit* caster)
{
    float range = kick->GetMaxRange(false, bot);
    if (!range)
        range = CasterCentredReach(bot, kick);
    if (range <= NOMINAL_MELEE_RANGE)
        return bot->IsWithinMeleeRange(caster);
    return bot->IsWithinCombatRange(caster, range);
}

// A kick the bot has ready that reaches the caster. Also read for other bots of the group, which
// is safe for members on the bot's own map instance: the same thread updates them.
// Whether this kick can work on that caster from this bot as it stands. Most classic dungeon bosses are immune to
// interrupts (CreatureImmunitiesId -325: Zum'rah, Antu'sul, Jammal'an...) and Ley Lock or Jawbreaker carry the
// interrupt mechanic, so they were spent on them while the heal finished (headless runs, 12/10). Toxic Dart needs
// stealth: a Ranger out of stealth tried it 436 times for no kick, and won the kick claim each time.
bool KickFits(Player* bot, SpellInfo const* kick, Unit* caster)
{
    if (kick->CheckShapeshift(bot->GetShapeshiftForm()) != SPELL_CAST_OK)
        return false;
    if (caster && caster->IsImmunedToSpell(kick, static_cast<Unit const*>(bot)))
        return false;
    // IsImmunedToSpell only says immune when every effect is: a kick that also hits (damage plus an interrupt effect
    // of mechanic 26) passed against a boss immune to interrupts, and only its damage landed (#28).
    if (caster)
    {
        bool stopper = false;
        bool landing = false;
        for (uint8 i = EFFECT_0; i < MAX_SPELL_EFFECTS; ++i)
        {
            SpellEffectInfo const& effect = kick->Effects[i];
            if (effect.Effect != SPELL_EFFECT_INTERRUPT_CAST &&
                !(effect.IsAura() && effect.ApplyAuraName == SPELL_AURA_MOD_SILENCE))
                continue;
            stopper = true;
            landing |= !caster->IsImmunedToSpellEffect(kick, i, bot);
        }
        if (stopper && !landing)
            return false;
    }
    // A kick with a cast time of its own (Ley Lock, half a second) lands after a cast that ends sooner: three heals
    // finished under a Ley Lock started in time to be too late (Zul'Farrak, 12/10). An instant kick goes first then.
    if (Spell* cast = caster ? InterruptibleSpell(caster) : nullptr)
        if (int32 const own = kick->CalcCastTime(bot))
            if (own + 100 >= cast->GetCastTimeRemaining())
                return false;
    return true;
}

SpellInfo const* ReadyKickFor(PlayerbotAI* ai, Player* member, Unit* caster)
{
    auto const* context = dynamic_cast<CoaAiObjectContext const*>(ai->GetAiObjectContext());
    if (!context)
        return nullptr;

    time_t const now = time(nullptr);
    for (Usable const& spell : KnownAbilities(member, [](uint16 kind) { return (kind & KIND_INTERRUPT) != 0; }))
    {
        SpellInfo const* const info = EffectiveSpell(member, spell.info);
        if (member->HasSpellCooldown(info->Id) || IsBenched(context->benchedSpells, spell.info->Id, now) ||
            IsBenched(context->benchedSpells, info->Id, now))
            continue;
        if (KickFits(member, info, caster) && KickReaches(member, info, caster))
            return spell.info;
    }
    return nullptr;
}

// The bot of the group that should kick this cast, and how many could have.
Player* ChooseKicker(Player* bot, Unit* caster, uint32& ready)
{
    Player* best = nullptr;
    int32 bestRank = 0;
    uint32 bestLast = 0;
    ready = 0;
    for (GroupReference* ref = bot->GetGroup()->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive() || !member->IsInCombat() || !OnSameInstance(bot, member) ||
            !ClassHas(member, KIND_INTERRUPT))
            continue;
        PlayerbotAI* ai = GET_PLAYERBOT_AI(member);
        if (!ai || !ReadyKickFor(ai, member, caster))
            continue;

        ++ready;
        CoaRole const role = GetCoaRole(member);
        int32 const rank = role == CoaRole::Tank ? 2 : role == CoaRole::Heal ? 1 : 0;
        uint32 const last = static_cast<CoaAiObjectContext*>(ai->GetAiObjectContext())->lastInterrupt;
        if (!best || rank < bestRank || (rank == bestRank && last < bestLast))
        {
            best = member;
            bestRank = rank;
            bestLast = last;
        }
    }
    return best;
}

// Whether this grouped bot is the one to kick the cast. Every grouped bot reports the casts it
// sees, coordination on or off, so that the counters compare the two.
bool MayKick(Player* bot, Unit* caster, Spell* cast)
{
    uint32 const now = getMSTime();
    uint32 const spellId = cast->GetSpellInfo()->Id;
    uint32 const remaining = uint32(std::max(cast->GetCastTimeRemaining(), 0));

    std::lock_guard<std::mutex> guard(InterruptClaimsLock);
    DropSpentClaims(now);
    auto itr = InterruptClaims.find(KeyOf(caster));
    if (itr != InterruptClaims.end() && itr->second.spellId != spellId)
    {
        Retire(itr->second);
        InterruptClaims.erase(itr);
        itr = InterruptClaims.end();
    }

    if (itr != InterruptClaims.end())
    {
        InterruptClaim& claim = itr->second;
        if (!CoordinateInterrupts() || claim.bot == bot->GetGUID())
            return true;
        if (claim.bot.IsEmpty())  // let through, kept for a worse cast
            return false;
        if (claim.kicked || !Passed(now, claim.handOver))
        {
            ++InterruptStats.waited;
            return false;
        }
        claim.bot = bot->GetGUID();
        claim.handOver = now + InterruptHandOverMs;
        ++InterruptStats.takenOver;
        return true;
    }

    InterruptClaim claim;
    claim.spellId = spellId;
    claim.danger = IsDangerousCast(cast) ||
                   (sPlayerbotAIConfig.coaDungeonPriorities && CoaDungeonPriorities::InterruptRank(cast->GetSpellInfo()) == 2);
    claim.until = now + remaining + InterruptLandMarginMs;
    if (!CoordinateInterrupts())
    {
        InterruptClaims.emplace(KeyOf(caster), claim);
        return true;
    }

    uint32 ready = 0;
    Player* const chosen = ChooseKicker(bot, caster, ready);
    if (!claim.danger && ready < 2 && bot->GetMap()->IsDungeon())
        ++InterruptStats.heldBack;
    else
    {
        claim.bot = chosen ? chosen->GetGUID() : bot->GetGUID();
        claim.handOver = now + std::min(InterruptHandOverMs, remaining / 2);
    }
    InterruptClaims.emplace(KeyOf(caster), claim);
    return claim.bot == bot->GetGUID();
}

// A grouped bot kicked: the cast is taken care of until the kick has landed. True when another
// bot had already kicked that very cast, a kick wasted.
bool NoteKick(Player* bot, Unit* caster, uint32 enemySpellId, SpellInfo const* kick)
{
    uint32 const now = getMSTime();
    uint32 const travel = kick->Speed > 0.0f ? uint32(bot->GetDistance(caster) / kick->Speed * IN_MILLISECONDS) : 0;

    std::lock_guard<std::mutex> guard(InterruptClaimsLock);
    InterruptClaim& claim = InterruptClaims[KeyOf(caster)];
    if (claim.spellId != enemySpellId)
    {
        if (claim.spellId)
            Retire(claim);
        claim = InterruptClaim();
        claim.spellId = enemySpellId;
    }

    bool const wasted = claim.kicked && claim.bot != bot->GetGUID() && !Passed(now, claim.until);
    claim.bot = bot->GetGUID();
    claim.kicked = true;
    claim.until = now + travel + InterruptLandMarginMs;
    ++InterruptStats.kicks;
    InterruptStats.wasted += wasted;
    return wasted;
}

// Whether the bot should go for this unit's cast: with coordination, only a cast one of its
// kicks reaches, and only if it is the bot's turn.
bool WorthKicking(PlayerbotAI* botAI, Player* bot, Unit* unit)
{
    if (!unit || !unit->IsAlive())
        return false;
    Spell* const cast = InterruptibleSpell(unit);
    if (!cast)
        return false;
    if (CoordinateInterrupts() && !ReadyKickFor(botAI, bot, unit))
        return false;
    return !bot->GetGroup() || MayKick(bot, unit, cast);
}

// The enemy to interrupt: the current target when it casts, else an attacker that does.
// With AiPlayerbot.CoaDungeonPriorities the casts are ranked first: a heal, crowd control or summon
// before an ordinary cast, a boss cast of the dungeon table before both, the current target first
// among equals. A kick spent on a Frostbolt was no longer there for the heal cast next to it.
Unit* FindCaster(PlayerbotAI* botAI, Player* bot)
{
    Unit* target = botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
    GuidVector const& attackers = botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();

    if (!sPlayerbotAIConfig.coaDungeonPriorities)
    {
        if (WorthKicking(botAI, bot, target))
            return target;

        for (ObjectGuid const guid : attackers)
        {
            Unit* attacker = botAI->GetUnit(guid);
            if (attacker && attacker != target && bot->IsWithinDistInMap(attacker, 30.0f) && WorthKicking(botAI, bot, attacker))
                return attacker;
        }

        return nullptr;
    }

    // Ranked without side effects; WorthKicking then claims the cast for the group, best first.
    std::vector<std::pair<uint8, Unit*>> casters;
    auto consider = [&casters](Unit* unit)
    {
        if (!unit || !unit->IsAlive())
            return;
        if (Spell* cast = InterruptibleSpell(unit))
            casters.emplace_back(CoaDungeonPriorities::InterruptRank(cast->GetSpellInfo()), unit);
    };
    consider(target);
    for (ObjectGuid const guid : attackers)
    {
        Unit* attacker = botAI->GetUnit(guid);
        if (attacker && attacker != target && bot->IsWithinDistInMap(attacker, 30.0f))
            consider(attacker);
    }
    std::stable_sort(casters.begin(), casters.end(), [](auto const& a, auto const& b) { return a.first > b.first; });

    for (auto const& [rank, caster] : casters)
        if (WorthKicking(botAI, bot, caster))
            return caster;

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

        // Taunts, even those that deal damage, stay out of the rotation on an enemy another tank holds; those that deal
        // none, on one that already hits the tank: a Felsworn Tyrant spent 9 of its 18 Betrays on a boss already on it,
        // its taunt then on cooldown when an add ran to a healer (bench, 10/10).
        bool const noTaunt = tank && HeldByOtherTank(bot, target);
        bool const onMe = tank && target->GetVictim() == bot;
        std::vector<Usable> const usable = KnownAbilities(bot, [tank, noTaunt, onMe](uint16 kind)
            { return IsAttack(kind, tank) && !((noTaunt || (onMe && !(kind & KIND_DAMAGE))) && (kind & KIND_TAUNT)); });
        if (usable.empty())
            return false;

        // A tank whose lead shrinks (someone at 70% of its threat on the target, or a target it has not touched yet)
        // opens with its abilities that build threat, which the rotation through the kit did not put first.
        if (tank && sPlayerbotAIConfig.coaSmartTank)
        {
            float const mine = target->GetThreatMgr().GetThreat(bot);
            float other = 0.0f;
            for (ThreatReference const* ref : target->GetThreatMgr().GetUnsortedThreatList())
                if (ref->GetVictim() != bot)
                    other = std::max(other, ref->GetThreat());
            if (other >= mine * 0.7f)
            {
                std::vector<Usable> threat;
                std::copy_if(usable.begin(), usable.end(), std::back_inserter(threat),
                             [](Usable const& spell) { return BuildsThreat(spell.info); });
                if (!threat.empty())
                    if (SpellInfo const* cast = CastFirst(botAI, bot, threat, target, USAGE_ATTACK))
                        return RecordUsage(USAGE_ATTACK, cast);
            }
        }

        // A tank's threat is its damage: its abilities that deal none (Cripple, Consume Magic, Manaburn) come after the
        // others. A Felsworn Tyrant spent a third of its global cooldowns on them and lost the boss (bench, 10/10).
        // They are tried first, each in turn; the others only when none of them can go.
        if (tank)
        {
            std::vector<Usable> damage;
            std::copy_if(usable.begin(), usable.end(), std::back_inserter(damage),
                         [](Usable const& spell) { return DealsDamage(spell.info); });
            DropChannelBreakers(bot, damage);
            if (!damage.empty())
            {
                size_t const start = nextDamage % damage.size();
                std::rotate(damage.begin(), damage.begin() + start, damage.end());
                if (SpellInfo const* cast = CastFirst(botAI, bot, damage, target, USAGE_ATTACK))
                {
                    auto const itr = std::find_if(damage.begin(), damage.end(),
                        [cast](Usable const& spell) { return spell.info == cast; });
                    nextDamage = start + size_t(std::distance(damage.begin(), itr)) + 1;
                    return RecordUsage(USAGE_ATTACK, cast);
                }
            }
        }

        if (sPlayerbotAIConfig.coaAttackLoop)
            return AttackThroughCastFirst(usable, target, saveMana);

        time_t const now = time(nullptr);

        // Abilities with a cooldown go off as soon as they are ready, an aura kept on the target (damage
        // over time, a debuff) when it is not running, the rest fill in; within each, the abilities that
        // fit the build best first. Ties rotate, starting after the last ability that went off, so a bot
        // uses its whole kit instead of spamming the first ability that works.
        std::vector<size_t> order(usable.size());
        std::iota(order.begin(), order.end(), size_t(0));
        auto const rank = [&](size_t index)
        {
            return std::make_tuple(-int32(AttackTier(usable[index].info)), -int64(usable[index].synergy),
                                   (index + usable.size() - next % usable.size()) % usable.size());
        };
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return rank(a) < rank(b); });

        for (size_t index : order)
        {
            SpellInfo const* info = usable[index].info;

            Strikes& strikes = failures[info->Id];
            if (strikes.benchedUntil > now)
                continue;

            if (AuraStillRunning(bot, info, target))
                continue;

            if (saveMana && info->PowerType == POWER_MANA && info->CalcPowerCost(bot, info->GetSchoolMask()) > 0)
                continue;

            if (!CanActNow(bot, info) || PlayerbotAI::WouldBeReflected(bot, info, target))
                continue;

            if (BreaksOwnChannel(bot, info) || StrictCheck(bot, info, target) != SPELL_CAST_OK)
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
    // AiPlayerbot.CoaAttackLoop (StevenLeclerc 9ee20729): the attack goes through CastFirst like the
    // other actions. The 1.6 loop below benches a spell for a minute after three refusals by
    // PlayerbotAI::CastSpell, strikes never decaying, while CastSpell also refuses for reasons that
    // pass in a tick (moving with a cast time, sitting, casting): a ranged bot moving all fight lost
    // its main spell. CastFirst stops the bot, benches 8 seconds and logs why; it also waits for
    // melee reach before a cleave and does not recast a summon while it is up.
    bool AttackThroughCastFirst(std::vector<Usable> usable, Unit* target, bool saveMana)
    {
        if (saveMana)
            DropManaSpells(bot, usable);
        DropChannelBreakers(bot, usable);
        if (usable.empty())
            return RecordUsage(USAGE_ATTACK, nullptr);

        // Start after the last ability that went off, so the whole kit gets used.
        size_t const start = next % usable.size();
        std::rotate(usable.begin(), usable.begin() + start, usable.end());

        SpellInfo const* const cast = CastFirst(botAI, bot, usable, target, USAGE_ATTACK);
        if (cast)
        {
            auto const itr = std::find_if(usable.begin(), usable.end(),
                [cast](Usable const& spell) { return spell.info == cast; });
            if (itr != usable.end())
                next = start + size_t(std::distance(usable.begin(), itr)) + 1;
        }

        return RecordUsage(USAGE_ATTACK, cast);
    }

    static constexpr uint8 MaxStrikes = 3;
    static constexpr time_t BenchSeconds = 60;

    struct Strikes
    {
        uint8 count = 0;
        time_t benchedUntil = 0;
    };

    size_t next = 0;
    size_t nextDamage = 0;  // a tank's turn through its damaging abilities
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
        DropChannelBreakers(bot, spells);
        std::stable_sort(spells.begin(), spells.end(), [](Usable const& a, Usable const& b) { return a.synergy > b.synergy; });

        return RecordUsage(USAGE_AOE, CastFirst(botAI, bot, spells, target, LoopUsage(USAGE_AOE)));
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
                if (SmartHeal())
                    OrderByGroupFit(bot, target, spells);
                break;
            case Mode::OverTime:
            {
                spells = KnownAbilities(bot, [](uint16 kind) { return (kind & KIND_HOT) && !(kind & (KIND_CONTROL | KIND_HOSTILE)); });
                ObjectGuid const caster = bot->GetGUID();
                spells.erase(std::remove_if(spells.begin(), spells.end(),
                    [target, caster](Usable const& spell) { return target->HasAura(spell.info->Id, caster); }),
                    spells.end());
                if (SmartHeal())
                    OrderByHotFit(bot, target, spells);
                break;
            }
            default:
            {
                // Single target heals first, direct ones before those over time; area heals last. A heal
                // over time and nothing else that this healer already keeps on the target is left out: it
                // would only be renewed, the cheapest of the list when the mana is saved and the instant
                // one while moving (shiro2448 402a235: a Hero renewed Rejuvenation on a dropping tank
                // until its mana ran out; 02/10: Shadra's Balm and Accelerated Recovery cast as direct heals).
                spells = KnownAbilities(bot, [](uint16 kind) { return (kind & KIND_HEAL) && !(kind & (KIND_CONTROL | KIND_HOSTILE)); });
                ObjectGuid const caster = bot->GetGUID();
                spells.erase(std::remove_if(spells.begin(), spells.end(), [target, caster](Usable const& spell)
                    { return IsPureHot(spell) && target->HasAura(spell.info->Id, caster); }),
                    spells.end());
                std::stable_sort(spells.begin(), spells.end(), [](Usable const& a, Usable const& b)
                {
                    auto rank = [](uint16 kind) { return ((kind & KIND_GROUP_HEAL) ? 2 : 0) + ((kind & KIND_HOT) ? 1 : 0); };
                    return rank(a.kind) < rank(b.kind);
                });
                break;
            }
        }

        bool const smart = SmartHeal() && mode == Mode::Direct;
        if (smart)
            OrderByUrgency(bot, target, spells);

        // Someone dropping is healed with the fastest heal even when mana is short; otherwise a
        // bot saving its mana goes for the cheapest.
        float const dropping = sPlayerbotAIConfig.coaFastHealBelowLow ? sPlayerbotAIConfig.lowHealth
                                                                       : sPlayerbotAIConfig.criticalHealth;
        if (SavingManaForHeals(bot) && !(smart && target->GetHealthPct() < dropping))
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
        // The heal over time kept on the tank before the hits land, while the mana allows it - but
        // only on a tank something is actually hitting: on an untouched one it ticks into full
        // health (23/09: 82% of a Chronomancer's Accelerated Recovery wasted that way).
        if (!target && mode == Mode::OverTime && bot->IsInCombat() &&
            (bot->getPowerType() != POWER_MANA || bot->GetPowerPct(POWER_MANA) >= 50.0f))
            if (Player* tank = GroupTank(bot))
                if (!tank->getAttackers().empty() && !CarriesAllHots(tank, KnownHots(bot), bot->GetGUID()))
                    target = tank;
        return target;
    }

    Mode mode;
};

bool GroupFighting(Player* bot);

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
        // While the group fights, a healer nobody hits is not in a fight itself and would stand idle -
        // its heals run in the combat engine: it heals at its usual line, and its first heal takes it
        // into the fight. Test arena: healers standing back healed nothing whole waves long.
        bool const groupFighting = GroupFighting(bot);
        Unit* target = SmartHealTarget(bot, groupFighting ? HealLine(bot) : float(sPlayerbotAIConfig.lowHealth));
        if (!target)
            return false;
        return groupFighting || target->GetHealthPct() < sPlayerbotAIConfig.criticalHealth ||
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
        // The class kit has one, this bot may not know it yet: a Fleshweaver fired this every update and
        // nothing answered the tank between 65 and 85% (ladiabale, Sunken Temple, 12/10).
        if (!HasReadyAbility(botAI, bot, [](uint16 kind) { return (kind & KIND_HOT) && !(kind & (KIND_CONTROL | KIND_HOSTILE)); }))
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
        // While the group fights, not only while this bot fights: a healer standing back is never
        // in a fight itself, and it is exactly the one that has to be brought back within reach.
        // Never in a battleground: there the bot follows its objective (flag run, base), not a tank.
        if (!SmartHeal() || bot->InBattleground() || !(bot->IsInCombat() || GroupFighting(bot)))
            return false;
        Player* tank = GroupTank(bot);
        return tank && (bot->GetDistance2d(tank) > StayNearTank + 4.0f || !bot->IsWithinLOSInMap(tank));
    }
};

// A healer out of healing reach of the tank or out of its sight, in a fight: no heal of it can land
// there. Its rotation outranks "coa far from tank" (priorities 30 to 92 against 18), so a Cultist
// healer left on the Deadmines platform kept casting Gaze of C'Thun and wanding the whole boss fight
// (jealous-sound #5094). This one outranks the rotation.
class CoaOutOfHealingReachTrigger : public Trigger
{
public:
    CoaOutOfHealingReachTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa out of healing reach") {}

    bool IsActive() override
    {
        // Not in a battleground either: at priority 95 it outranked the flag carrier's run home (90).
        if (!SmartHeal() || GetCoaRole(bot) != CoaRole::Heal || bot->InBattleground() ||
            !(bot->IsInCombat() || GroupFighting(bot)))
            return false;
        Player* tank = GroupTank(bot);
        return tank && (bot->GetDistance(tank) > sPlayerbotAIConfig.healDistance || !bot->IsWithinLOSInMap(tank));
    }
};

// Moves back within StayNearTank of the tank, between two casts.
class CoaStayNearTankAction : public MovementAction
{
public:
    CoaStayNearTankAction(PlayerbotAI* botAI) : MovementAction(botAI, "coa stay near tank") {}
    CoaStayNearTankAction(PlayerbotAI* botAI, char const* name, float keep)
        : MovementAction(botAI, name), keep(keep) {}

    bool Execute(Event /*event*/) override
    {
        Player* tank = GroupTank(bot);
        return tank && MoveNear(tank, keep);
    }

    bool isUseful() override { return !bot->IsNonMeleeSpellCast(false, true, true); }

private:
    float keep = StayNearTank - 4.0f;
};

// A healer that the pull left behind joins the fight, but only as far as it has to: stopping at
// the tank's own distance puts it inside the pack, and it died twice as often (23/09).
class CoaReachHealingRangeAction : public CoaStayNearTankAction
{
public:
    CoaReachHealingRangeAction(PlayerbotAI* botAI)
        : CoaStayNearTankAction(botAI, "coa reach healing range", StayNearTank - 2.0f) {}
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
        return bot->GetGroup() && GroupPlayer(botAI, bot);
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

// An enemy hitting another player of the tank's group, within taunt reach: the one on a healer first,
// then the one whose victim is the lowest. Tanks only taunted their own target when it turned away, and
// left alone the mobs that never were their target: 0.25 to 0.86 taunts a fight, none in 39 to 79% of
// fights, while 80% of the deaths were damage dealers and healers (NUC dungeon arenas, 28/09).
// An enemy on another tank is held, not loose, even for the main tank: its adds stay with the off-tank.
Unit* LooseEnemy(PlayerbotAI* botAI, Player* bot)
{
    Group* group = bot->GetGroup();
    if (!group)
        return nullptr;
    Unit* best = nullptr;
    int32 bestScore = INT32_MIN;
    for (ObjectGuid const& guid : botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get())
    {
        Unit* enemy = botAI->GetUnit(guid);
        if (!enemy || !enemy->IsAlive() || enemy->IsPlayer())
            continue;
        Unit* victim = enemy->GetVictim();
        Player* victimPlayer = victim ? victim->ToPlayer() : nullptr;
        if (!victimPlayer || victimPlayer == bot || victimPlayer->GetGroup() != group ||
            PlayerbotAI::IsTank(victimPlayer))
            continue;
        if (bot->GetDistance(enemy) > 30.0f || !bot->IsWithinLOSInMap(enemy))
            continue;
        int32 score = 100 - int32(victimPlayer->GetHealthPct());
        if (GetCoaRole(victimPlayer) == CoaRole::Heal)
            score += 100;
        if (score > bestScore)
        {
            best = enemy;
            bestScore = score;
        }
    }
    return best;
}

// Whether the current target attacks someone else than the bot, and not another tank holding it.
bool OwnTargetTurned(Player* bot, Unit* target)
{
    return target && target->IsAlive() && target->GetVictim() && target->GetVictim() != bot &&
           !HeldByOtherTank(bot, target);
}

// Takes the current target back when it attacks someone else, or else an enemy on another member.
class CoaTauntAction : public Action
{
public:
    CoaTauntAction(PlayerbotAI* botAI) : Action(botAI, "coa taunt") {}

    bool Execute(Event /*event*/) override
    {
        Unit* target = AI_VALUE(Unit*, "current target");
        bool const ownTurned = OwnTargetTurned(bot, target);
        if (!ownTurned)
            target = LooseEnemy(botAI, bot);
        if (!target || !target->IsAlive())
            return false;

        SpellInfo const* taunt = CastFirst(botAI, bot,
            KnownAbilities(bot, [](uint16 kind) { return (kind & KIND_TAUNT) != 0; }), target, USAGE_TAUNT);
        if (taunt)
        {
            CoaTelemetryNoteTaunt(bot);
            // Taken back: it becomes the target the tank now holds.
            if (!ownTurned)
                context->GetValue<Unit*>("current target")->Set(target);
        }
        return RecordUsage(USAGE_TAUNT, taunt);
    }

    bool isUseful() override
    {
        if (!ClassHas(bot, KIND_TAUNT) || !HasReadyAbility(botAI, bot, [](uint16 kind) { return (kind & KIND_TAUNT) != 0; }))
            return false;
        if (OwnTargetTurned(bot, AI_VALUE(Unit*, "current target")))
            return true;
        return LooseEnemy(botAI, bot) != nullptr;
    }
};

// For a tank: an enemy is hitting another player of its group.
class CoaLooseEnemyTrigger : public Trigger
{
public:
    CoaLooseEnemyTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa loose enemy", 1) {}

    bool IsActive() override { return bot->IsInCombat() && LooseEnemy(botAI, bot) != nullptr; }
};

// When hurt: a defensive cooldown, or failing that a heal on itself.
class CoaDefensiveAction : public Action
{
public:
    CoaDefensiveAction(PlayerbotAI* botAI) : Action(botAI, "coa defensive") {}

    bool Execute(Event /*event*/) override
    {
        SpellInfo const* cast =
            CastFirst(botAI, bot, KnownAbilities(bot, [](uint16 kind) { return (kind & KIND_DEFENSIVE) != 0; }), bot,
                      LoopUsage(USAGE_DEFENSIVE));
        if (!cast)
        {
            std::vector<Usable> heals = KnownAbilities(bot, [](uint16 kind)
                { return (kind & KIND_HEAL) && !(kind & (KIND_GROUP_HEAL | KIND_CONTROL | KIND_HOSTILE)); });
            if (SavingManaForHeals(bot))
                CheapestFirst(bot, heals);

            cast = CastFirst(botAI, bot, heals, bot, LoopUsage(USAGE_DEFENSIVE));
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
        std::vector<Usable> const spells = KnownAbilities(bot, IsGroupDispel);
        if (spells.empty())
            return false;

        std::vector<Player*> members = NearbyGroup(bot);
        // AiPlayerbot.CoaSmartDispel: a member who lost control to a dispellable aura first, healers
        // then tanks before the others, and a member another bot is cleansing is left to it.
        if (sPlayerbotAIConfig.coaSmartDispel)
        {
            uint32 allMasks = 0;
            for (Usable const& spell : spells)
                allMasks |= spell.dispelMask;

            auto rank = [allMasks](Player* member)
            {
                int score = HasDispellableControl(member, allMasks) ? 0 : 3;
                CoaRole const role = GetCoaRole(member);
                score += role == CoaRole::Heal ? 0 : role == CoaRole::Tank ? 1 : 2;
                return score;
            };
            std::stable_sort(members.begin(), members.end(),
                             [&rank](Player* a, Player* b) { return rank(a) < rank(b); });
        }

        for (Player* member : members)
        {
            if (sPlayerbotAIConfig.coaSmartDispel && member != bot && DispelClaimed(bot, member))
                continue;

            for (Usable const& spell : spells)
            {
                // Some cleanses only work on the caster.
                Unit* const castOn = DispelTarget(bot, spell, member);
                if (!castOn)
                    continue;

                if (!HasDispellable(member, spell.dispelMask))
                    continue;

                if (SpellInfo const* cast = CastFirst(botAI, bot, { spell }, castOn, USAGE_DISPEL))
                    return RecordUsage(USAGE_DISPEL, cast);
            }
        }

        return RecordUsage(USAGE_DISPEL, nullptr);
    }

    bool isUseful() override { return ClassHas(bot, KIND_DISPEL) && HasReadyAbility(botAI, bot, IsGroupDispel); }
};

/*
 * Purge and soothe (AiPlayerbot.CoaDungeonPriorities). In a dungeon, a bot with an ability that
 * takes a magic buff or an enrage off an enemy uses it on a boss or elite of the fight that carries
 * one: a Lightning Shield, a Demon Armor or the Enrage at half health stayed on the boss the whole
 * fight, since nothing aimed these abilities (the attack rotation only cast them by chance). Once a
 * bot has cast at a buff the others leave that buff alone for a moment, or every purge of the group
 * went at the same Frost Ward.
 */
constexpr uint32 PurgeClaimMs = 2500;

using PurgeKey = std::tuple<uint32, uint32, uint64, uint32>;  // map, instance, enemy guid, buff
std::mutex PurgeClaimsLock;
std::map<PurgeKey, uint32> PurgeClaims;  // -> getMSTime() until which the other bots leave it

PurgeKey PurgeKeyOf(Unit* enemy, uint32 buffId)
{
    return { enemy->GetMapId(), enemy->GetInstanceId(), enemy->GetGUID().GetRawValue(), buffId };
}

bool PurgeClaimed(Unit* enemy, uint32 buffId, uint32 now)
{
    std::lock_guard<std::mutex> guard(PurgeClaimsLock);
    auto const found = PurgeClaims.find(PurgeKeyOf(enemy, buffId));
    return found != PurgeClaims.end() && !Passed(now, found->second);
}

void ClaimPurge(Unit* enemy, uint32 buffId, uint32 now)
{
    std::lock_guard<std::mutex> guard(PurgeClaimsLock);
    for (auto itr = PurgeClaims.begin(); itr != PurgeClaims.end();)
        if (Passed(now, itr->second))
            itr = PurgeClaims.erase(itr);
        else
            ++itr;
    PurgeClaims[PurgeKeyOf(enemy, buffId)] = now + PurgeClaimMs;
}

// A buff on the enemy of one of the dispel types in `mask` that no other bot is taking off. Passive
// and permanent auras are left out: they are part of the creature, not something it cast.
SpellInfo const* PurgeableBuff(Unit* enemy, uint32 mask, uint32 now)
{
    for (auto const& applied : enemy->GetAppliedAuras())
    {
        AuraApplication const* application = applied.second;
        if (!application->IsPositive())
            continue;

        Aura const* aura = application->GetBase();
        SpellInfo const* info = aura->GetSpellInfo();
        if (aura->IsPassive() || aura->GetMaxDuration() <= 0 || !info->Dispel || !(mask & (1 << info->Dispel)))
            continue;
        if (!PurgeClaimed(enemy, info->Id, now))
            return info;
    }
    return nullptr;
}

bool ClassPurges(Player* bot)
{
    auto const& all = ClassAbilities();
    auto const found = all.find(bot->getClass());
    return found != all.end() && found->second.purgeMask;
}

std::vector<Usable> KnownPurges(Player* bot)
{
    std::vector<Usable> purges = KnownAbilities(bot, [](uint16 kind) { return (kind & KIND_HOSTILE) != 0; });
    purges.erase(std::remove_if(purges.begin(), purges.end(), [](Usable const& spell) { return !spell.purgeMask; }),
                 purges.end());
    return purges;
}

struct PurgeChoice
{
    Unit* enemy = nullptr;
    SpellInfo const* buff = nullptr;
};

// The boss or elite of the fight within 30 yards to purge, the current target first, with the buff to
// take off; nothing when no purge of the bot is ready.
PurgeChoice FindPurge(PlayerbotAI* botAI, Player* bot, std::vector<Usable> const& purges)
{
    PurgeChoice choice;
    // AiPlayerbot.CoaArenaPurge: in an arena, the enemy players' magic buffs too.
    bool const arena = sPlayerbotAIConfig.coaArenaPurge && bot->InArena();
    if (!bot->IsInCombat() || (!arena && (!sPlayerbotAIConfig.coaDungeonPriorities || !bot->GetMap()->IsDungeon())))
        return choice;

    auto const& benched = static_cast<CoaAiObjectContext*>(botAI->GetAiObjectContext())->benchedSpells;
    time_t const now = time(nullptr);
    uint32 mask = 0;
    // The reach of the ready purges: Gavel of Wrath reaches 5 yd, and aimed at enemies up to 30 yd away it landed
    // once in 324 tries.
    float reach = 0.0f;
    for (Usable const& spell : purges)
    {
        SpellInfo const* const info = EffectiveSpell(bot, spell.info);
        if (!bot->HasSpellCooldown(info->Id) && !IsBenched(benched, spell.info->Id, now) && !IsBenched(benched, info->Id, now))
        {
            mask |= spell.purgeMask;
            reach = std::max(reach, info->GetMaxRange(false, bot));
        }
    }
    reach = std::min(reach > 0.0f ? reach : 30.0f, 30.0f);
    if (!mask)
        return choice;

    uint32 const nowMs = getMSTime();
    auto consider = [&](Unit* unit)
    {
        if (arena)
        {
            Player* enemy = unit ? unit->ToPlayer() : nullptr;
            if (!enemy || !enemy->IsAlive() || !bot->IsHostileTo(enemy) || !bot->IsWithinDistInMap(enemy, reach) ||
                !bot->IsWithinLOSInMap(enemy))
                return false;
            if (SpellInfo const* buff = PurgeableBuff(enemy, mask, nowMs))
            {
                choice.enemy = enemy;
                choice.buff = buff;
                return true;
            }
            return false;
        }

        Creature* creature = unit ? unit->ToCreature() : nullptr;
        if (!creature || !creature->IsAlive() ||
            !(creature->IsDungeonBoss() || creature->isWorldBoss() || creature->isElite()) ||
            !bot->IsWithinDistInMap(creature, reach))
            return false;
        if (SpellInfo const* buff = PurgeableBuff(creature, mask, nowMs))
        {
            choice.enemy = creature;
            choice.buff = buff;
            return true;
        }
        return false;
    };

    Unit* target = botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
    if (consider(target))
        return choice;
    for (ObjectGuid const guid : botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get())
    {
        Unit* attacker = botAI->GetUnit(guid);
        if (attacker != target && consider(attacker))
            return choice;
    }
    return choice;
}

class CoaPurgeAction : public Action
{
public:
    CoaPurgeAction(PlayerbotAI* botAI) : Action(botAI, "coa purge") {}

    bool Execute(Event /*event*/) override
    {
        std::vector<Usable> purges = KnownPurges(bot);
        PurgeChoice const choice = FindPurge(botAI, bot, purges);
        if (!choice.enemy)
            return false;

        uint32 const type = 1 << choice.buff->Dispel;
        purges.erase(std::remove_if(purges.begin(), purges.end(),
                                    [type](Usable const& spell) { return !(spell.purgeMask & type); }),
                     purges.end());
        SpellInfo const* cast = CastFirst(botAI, bot, purges, choice.enemy, USAGE_DISPEL);
        if (cast)
        {
            ClaimPurge(choice.enemy, choice.buff->Id, getMSTime());
            if (CoaDungeonPriorities::LogDue(CoaDungeonPriorities::Decision::Purge))
                LOG_INFO("playerbots", "coa dungeon: {} cast {} ({}) to take {} ({}) off {}", bot->GetName(),
                         cast->SpellName[LOCALE_enUS], cast->Id, choice.buff->SpellName[LOCALE_enUS], choice.buff->Id,
                         choice.enemy->GetName());
        }
        return RecordUsage(USAGE_DISPEL, cast);
    }

    bool isUseful() override { return sPlayerbotAIConfig.coaDungeonPriorities && ClassPurges(bot); }
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

        // Read before the kick, which ends the cast.
        SpellInfo const* const enemySpell = InterruptibleSpell(caster)->GetSpellInfo();
        uint32 const enemySpellId = enemySpell->Id;
        std::vector<Usable> kicks = KnownAbilities(bot, [](uint16 kind) { return (kind & KIND_INTERRUPT) != 0; });
        kicks.erase(std::remove_if(kicks.begin(), kicks.end(),
                                   [&](Usable const& k) { return !KickFits(bot, EffectiveSpell(bot, k.info), caster); }),
                    kicks.end());
        if (kicks.empty())
            return false;
        // The strict check before the cast refuses a kick at a caster behind the bot ("not in front" was the first
        // kick failure of those runs) and the facing in PlayerbotAI::CastSpell comes after it: turn first.
        if (!bot->HasInArc(CAST_ANGLE_IN_FRONT, caster))
            ServerFacade::instance().SetFacingTo(bot, caster);
        SpellInfo const* kick = CastFirst(botAI, bot, kicks, caster, LoopUsage(USAGE_INTERRUPT));
        if (kick && sPlayerbotAIConfig.coaDungeonPriorities)
            if (uint8 const rank = CoaDungeonPriorities::InterruptRank(enemySpell))
                if (CoaDungeonPriorities::LogDue(CoaDungeonPriorities::Decision::Kick))
                    LOG_INFO("playerbots", "coa dungeon: {} kicked {} ({}) of {}, priority {} ({})", bot->GetName(),
                             enemySpell->SpellName[LOCALE_enUS], enemySpellId, caster->GetName(), rank,
                             rank == 2 ? "dungeon table" : "heal, control or summon");
        if (kick && bot->GetGroup())
        {
            static_cast<CoaAiObjectContext*>(botAI->GetAiObjectContext())->lastInterrupt = getMSTime();
            CoaTelemetryNoteInterrupt(bot, NoteKick(bot, caster, enemySpellId, kick));
        }

        return RecordUsage(USAGE_INTERRUPT, kick);
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
            { return (spell.kind & KIND_STANCE) && StandsIn(bot, spell.info); });

        // A tank takes its tank stance, the one raising its threat (Felsworn Agonizing Presence), in
        // place of any other it stood in: the first stance known was taken and kept, and tanks were
        // seen in dungeons without theirs (jealous-sound #5063, #5173). The others never take it.
        bool const tank = GetCoaRole(bot) == CoaRole::Tank;
        bool const knowsTankStance = tank && std::any_of(spells.begin(), spells.end(), [](Usable const& spell)
            { return (spell.kind & KIND_STANCE) && RaisesThreat(spell.info); });
        // Worn, not merely of the same name: Reaper has two "Dominion", the tank stance 807735 and a proc
        // buff 803999, and the second one made a tank believe it stood in its stance.
        bool const inTankStance = WearsThreatStance(bot);

        // A tank none of whose stances raises its threat (Guardian Vanguard, Venomancer Fortitude) takes only the
        // stance its rotation asks for: the first one known was Assault Formation or Spider Form, damage stances that
        // left it with little threat (jealous-sound/azerothcore-wotlk-coa#6877). One it wears besides comes off.
        bool const tankWithoutThreatStance = tank && !knowsTankStance;
        if (tankWithoutThreatStance)
            for (Usable const& spell : spells)
                if ((spell.kind & KIND_STANCE) && !IsTravelForm(spell.info) && bot->HasAura(spell.info->Id, bot->GetGUID()) &&
                    !RotationAsksBuff(botAI, spell.info))
                    bot->RemoveAurasDueToSpell(spell.info->Id, bot->GetGUID());

        // A damage dealer or a healer standing in a tank stance or wearing a buff of its own that raises its
        // threat (its rotation used to put them on) takes it off.
        if (!tank)
            for (Usable const& spell : spells)
                if (RaisesThreat(spell.info) && bot->HasAura(spell.info->Id, bot->GetGUID()))
                    bot->RemoveAurasDueToSpell(spell.info->Id, bot->GetGUID());

        time_t const now = time(nullptr);
        if (recent.size() > 64)
            for (auto itr = recent.begin(); itr != recent.end();)
                itr = itr->second.until <= now ? recent.erase(itr) : std::next(itr);

        // A member seen wearing what the bot put on, who no longer wears it (raised from the dead,
        // dispelled), gets it again now: the wait is for an aura not seen yet, under another id
        // (AiPlayerbot.CoaExclusiveFamilies).
        std::vector<Player*> const group = NearbyGroup(bot);
        if (sPlayerbotAIConfig.coaExclusiveFamilies)
            for (auto& [key, cast] : recent)
                for (Player* member : group)
                    if (member->GetGUID() == key.first)
                        if (SpellInfo const* info = sSpellMgr->GetSpellInfo(key.second))
                        {
                            if (WearsBuffFrom(member, bot, info))
                                cast.seen = true;
                            else if (cast.seen)
                                cast.until = 0;
                        }

        for (Usable const& spell : spells)
            for (Player* member : group)
            {
                if (member != bot && !(spell.kind & KIND_ALLY_CAST))
                    continue;

                // A buff of under a minute is a fight cooldown, not one to keep up between fights: a Bloodmage
                // Fleshweaver cast Scarlet Delirium (40 s) again and again out of combat (player report, 10/10).
                if (!(spell.kind & KIND_STANCE) && spell.info->GetMaxDuration() > 0 && spell.info->GetMaxDuration() < 60000)
                    continue;

                // A buff raising its bearer's threat goes on a tank only.
                if (!(spell.kind & KIND_STANCE) && RaisesThreat(spell.info) && !PlayerbotAI::IsTank(member))
                    continue;

                // A stance is the bot's own, and only when it stands in none.
                if (spell.kind & KIND_STANCE)
                {
                    bool const threat = RaisesThreat(spell.info);
                    // A travel form (Sea Serpent Form: swim speed, water breathing) is no stance to fight in.
                    if (member != bot || (threat && !tank) || IsTravelForm(spell.info) || tankWithoutThreatStance)
                        continue;
                    if (knowsTankStance ? (!threat || inTankStance) : inStance)
                        continue;
                }

                // A healer keeps out of a form its heals cannot be cast in.
                if ((spell.kind & KIND_STANCE) && GetCoaRole(bot) == CoaRole::Heal && FormBlocksHeals(bot, spell.info))
                    continue;

                if (member->HasAura(spell.info->Id))
                    continue;

                // One buff per category: several of a displacing group would
                // chase each other forever. See HasBuffOfCategory.
                if (HasBuffOfCategory(member, spell.info->GetCategory(), spell.info->Id))
                    continue;

                // Nor the same buff over its greater form or the other way round, whoever cast it: a Witch
                // Hunter bot put Knight's Edict on a player who wore Greater Knight's Edict (#5385).
                if (WearsBuffNamed(member, spell.info))
                    continue;

                // Nor a second seal of the same caster: CoA classes give the buffs a caster keeps one of
                // on each target the same family flag (Seals of Al'ar and Alysrazor, Adaptations, Edicts).
                if (!(spell.kind & KIND_STANCE) && WearsRivalBuffFrom(member, bot, spell.info))
                    continue;

                // Nor a buff of a family the world database makes exclusive while the member wears
                // another of it from the bot (or from anyone, for a fully exclusive family), nor, on the
                // bot itself, another member than the one its rotation keeps (see CoaRotationMayCast).
                // The rotation lines only look at the bot: the others, a member just raised from the
                // dead included, still get one (AiPlayerbot.CoaExclusiveFamilies).
                if (sPlayerbotAIConfig.coaExclusiveFamilies &&
                    (WornExclusiveSibling(member, bot, spell.info) || (member == bot && RotationKeepsSibling(botAI, spell.info))))
                    continue;

                // The aura may come from a triggered spell under another id: do not recast
                // it on the same member before it would have run out.
                auto const key = std::make_pair(member->GetGUID(), spell.info->Id);
                auto const found = recent.find(key);
                if (found != recent.end() && found->second.until > now)
                    continue;

                if (StrictCheck(bot, spell.info, member) == SPELL_CAST_OK && botAI->CastSpell(spell.info->Id, member))
                {
                    recent[key] = { now + time_t(spell.info->GetMaxDuration() / IN_MILLISECONDS * 9 / 10), false };
                    return RecordUsage(USAGE_BUFF, spell.info);
                }
            }

        return false;
    }

    bool isUseful() override { return ClassHas(bot, KIND_BUFF | KIND_STANCE); }

private:
    struct RecentBuff
    {
        time_t until;
        bool seen;
    };
    std::map<std::pair<ObjectGuid, uint32>, RecentBuff> recent;
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

        // Only the cleanses ready now: a Ranger whose one cleanse able to clear her debuff was on
        // cooldown kept firing the action, which found nothing to cast (24 silent failures, ladiabale).
        std::vector<Usable> const spells = KnownAbilities(bot, IsGroupDispel);
        for (Player* member : NearbyGroup(bot))
            for (Usable const& spell : spells)
                if (DispelTarget(bot, spell, member) && HasDispellable(member, spell.dispelMask) &&
                    !bot->HasSpellCooldown(EffectiveSpell(bot, spell.info)->Id))
                    return true;

        return false;
    }
};

// AiPlayerbot.CoaSmartDispel: a group member is stunned, feared, polymorphed or silenced by an aura
// the bot can take off. Same action as "coa dispel", at a higher priority: a healer frees a
// controlled tank before it tops anyone up.
class CoaDispelControlTrigger : public Trigger
{
public:
    CoaDispelControlTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa dispel control") {}

    bool IsActive() override
    {
        // a dispel on cooldown or under a silence: 165 tries and no cast in one run
        if (!sPlayerbotAIConfig.coaSmartDispel || !ClassHas(bot, KIND_DISPEL) || !HasReadyAbility(botAI, bot, IsGroupDispel))
            return false;

        std::vector<Usable> const spells = KnownAbilities(bot, IsGroupDispel);
        for (Player* member : NearbyGroup(bot))
            for (Usable const& spell : spells)
                if (DispelTarget(bot, spell, member) && HasDispellableControl(member, spell.dispelMask))
                    return true;

        return false;
    }
};

/*
 * AiPlayerbot.CoaPvpTrinket: in an arena, a battleground or a duel, a CoA bot that loses control (stun,
 * fear, polymorph, charm, horror) for 2 seconds or more breaks free with the PvP trinket it wears (spell
 * 42292, the one PlayerbotFactory equips). Bots wore it and never used it: "every man for himself" is a
 * racial of the human race only, and the CoA strategies had no use for trinkets in a fight.
 */
constexpr uint32 SPELL_PVP_TRINKET = 42292;

bool InPvpFight(Player* bot)
{
    return bot->InArena() || bot->InBattleground() || bot->duel;
}

// The equipped trinket that casts the PvP trinket spell, if any.
Item* PvpTrinket(Player* bot)
{
    for (uint8 slot : { EQUIPMENT_SLOT_TRINKET1, EQUIPMENT_SLOT_TRINKET2 })
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            for (_Spell const& spell : item->GetTemplate()->Spells)
                if (spell.SpellId == SPELL_PVP_TRINKET)
                    return item;
    return nullptr;
}

// The longest time left on an aura that takes control of the bot, in milliseconds.
int32 LostControlFor(Player* bot)
{
    int32 longest = 0;
    for (AuraType type : { SPELL_AURA_MOD_STUN, SPELL_AURA_MOD_FEAR, SPELL_AURA_MOD_CONFUSE, SPELL_AURA_MOD_CHARM })
        for (AuraEffect const* effect : bot->GetAuraEffectsByType(type))
        {
            int32 const left = effect->GetBase()->GetDuration();
            longest = std::max(longest, left < 0 ? 60000 : left);
        }
    return longest;
}

class CoaPvpTrinketTrigger : public Trigger
{
public:
    CoaPvpTrinketTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa pvp trinket") {}

    bool IsActive() override
    {
        return sPlayerbotAIConfig.coaPvpTrinket && InPvpFight(bot) && LostControlFor(bot) >= 2000 &&
               !bot->HasSpellCooldown(SPELL_PVP_TRINKET) && PvpTrinket(bot);
    }
};

class CoaPvpTrinketAction : public Action
{
public:
    CoaPvpTrinketAction(PlayerbotAI* botAI) : Action(botAI, "coa pvp trinket") {}

    bool Execute(Event /*event*/) override
    {
        Item* const trinket = PvpTrinket(bot);
        if (!trinket || bot->HasSpellCooldown(SPELL_PVP_TRINKET))
            return false;
        return bot->CastSpell(bot, SPELL_PVP_TRINKET, false, trinket) == SPELL_CAST_OK;
    }
};

/*
 * AiPlayerbot.CoaAvoidMarkedAlly: a harmful aura that makes its carrier hurt the allies around him (a
 * bomb, a brand, a beacon) is read from the spell data: an aura that triggers, or spreads, a damaging
 * or controlling spell on the carrier's allies in an area. A spell meant to be shared (damage split
 * among those it hits) is left out: the group has to stack for it. Radius 0: no danger.
 * Idea from the boss scripts of the forks (ArkosLUL a14a69517fc, XT-002 Gravity Bomb carriers; the
 * MoveAwayFromPlayerWithDebuffAction of Yogg-Saron, Baltharus, Saviana), made generic for CoA content.
 */
bool HurtsCarrierAllies(SpellEffectInfo const& effect)
{
    switch (effect.TargetA.GetTarget())
    {
        case TARGET_UNIT_CASTER_AREA_PARTY: case TARGET_UNIT_SRC_AREA_ALLY: case TARGET_UNIT_SRC_AREA_PARTY:
        case TARGET_UNIT_CASTER_AREA_RAID: case TARGET_UNIT_DEST_AREA_ALLY:
            break;
        default:
            return false;
    }

    return IsDamage(effect) || (IsAuraEffect(effect) && IsControlAura(effect));
}

float MarkRadius(SpellInfo const* aura)
{
    static std::mutex lock;
    static std::unordered_map<uint32, float> radii;
    {
        std::lock_guard<std::mutex> guard(lock);
        auto const known = radii.find(aura->Id);
        if (known != radii.end())
            return known->second;
    }

    float radius = 0.0f;
    for (SpellEffectInfo const& effect : aura->Effects)
    {
        // An aura spread to the carrier's allies around him.
        if ((effect.Effect == SPELL_EFFECT_APPLY_AREA_AURA_FRIEND || effect.Effect == SPELL_EFFECT_APPLY_AREA_AURA_PARTY ||
             effect.Effect == SPELL_EFFECT_APPLY_AREA_AURA_RAID) &&
            (effect.ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE || IsControlAura(effect)))
            radius = std::max(radius, effect.CalcRadius());

        // What the aura casts from the carrier, now (periodic) or when it ends (trigger rider).
        if (!effect.TriggerSpell)
            continue;
        SpellInfo const* triggered = sSpellMgr->GetSpellInfo(effect.TriggerSpell);
        if (!triggered || triggered->HasAttribute(SPELL_ATTR0_CU_SHARE_DAMAGE))
            continue;
        for (SpellEffectInfo const& hit : triggered->Effects)
            if (HurtsCarrierAllies(hit))
                radius = std::max(radius, hit.CalcRadius());
    }

    // Beyond this the spot search below finds nothing anyway.
    radius = std::min(radius, 30.0f);

    std::lock_guard<std::mutex> guard(lock);
    radii[aura->Id] = radius;
    return radius;
}

// The radius of the most dangerous mark the unit carries, 0 without one.
float CarriedMarkRadius(Unit* unit)
{
    float radius = 0.0f;
    for (auto const& [auraId, application] : unit->GetAppliedAuras())
        if (!application->IsPositive())
            radius = std::max(radius, MarkRadius(application->GetBase()->GetSpellInfo()));
    return radius;
}

struct MarkedHazard
{
    Player* center;
    float radius;
};

// What the bot has to keep away from: every marked member around it, or, when the bot carries the
// mark, every other member.
std::vector<MarkedHazard> MarkedHazards(Player* bot)
{
    std::vector<MarkedHazard> hazards;
    if (!sPlayerbotAIConfig.coaAvoidMarkedAlly || !bot->GetGroup() || !bot->IsInCombat())
        return hazards;

    std::vector<Player*> const members = NearbyGroup(bot);
    if (float const own = CarriedMarkRadius(bot))
    {
        for (Player* member : members)
            if (member != bot)
                hazards.push_back({ member, own });
        return hazards;
    }

    for (Player* member : members)
        if (member != bot)
            if (float const radius = CarriedMarkRadius(member))
                hazards.push_back({ member, radius });
    return hazards;
}

class CoaMarkedAllyTrigger : public Trigger
{
public:
    CoaMarkedAllyTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa marked ally", 1) {}

    bool IsActive() override
    {
        for (MarkedHazard const& hazard : MarkedHazards(bot))
            if (bot->GetExactDist2d(hazard.center) < hazard.radius + 1.0f)
                return true;
        return false;
    }
};

// Steps out of the reach of the marked members, or away from everyone while the bot carries the mark.
class CoaAvoidMarkedAllyAction : public MovementAction
{
public:
    CoaAvoidMarkedAllyAction(PlayerbotAI* botAI) : MovementAction(botAI, "coa avoid marked ally") {}

    bool Execute(Event /*event*/) override
    {
        std::vector<MarkedHazard> const hazards = MarkedHazards(bot);
        if (hazards.empty())
            return false;

        float bestX = 0.0f, bestY = 0.0f, bestScore = -1.0f;
        float const z = bot->GetPositionZ();
        for (int i = 0; i < 16; ++i)
        {
            float const angle = i * 2.0f * float(M_PI) / 16.0f;
            for (float step = 3.0f; step <= 33.0f; step += 3.0f)
            {
                float const x = bot->GetPositionX() + step * std::cos(angle);
                float const y = bot->GetPositionY() + step * std::sin(angle);
                float margin = std::numeric_limits<float>::max();
                for (MarkedHazard const& hazard : hazards)
                    margin = std::min(margin, hazard.center->GetExactDist2d(x, y) - hazard.radius - 2.0f);
                if (margin < 0.0f || !bot->IsWithinLOS(x, y, z))
                    continue;
                // The nearest safe spot, not the farthest: the bot keeps fighting from there.
                float const score = 100.0f - step;
                if (score > bestScore)
                {
                    bestScore = score;
                    bestX = x;
                    bestY = y;
                }
                break;
            }
        }

        if (bestScore < 0.0f)
            return false;

        return MoveTo(bot->GetMapId(), bestX, bestY, z, false, false, false, false, MovementPriority::MOVEMENT_COMBAT, true);
    }

    bool isPossible() override { return bot->CanFreeMove(); }
};

/*
 * Adds first (AiPlayerbot.CoaDungeonPriorities). In a dungeon fight a damage dealer switches to a
 * totem, ward or add that heals or shields its side (see CoaDungeonPriorities::HealsOrProtects)
 * within 30 yards: bots kept hitting the boss while a Healing Ward healed it back, and an
 * Earthgrab Totem rooted the group the whole fight. Tanks keep the boss, healers keep healing.
 *
 * The switch goes through the target choice of mod-playerbots: the add is put in "prioritized
 * targets", which brings it among the attackers (a totem has no threat list, so it never was one)
 * and makes it the "dps target", so "dps assist" keeps it instead of switching back. A list the
 * player set ("attack my target", a raid icon) is left alone while its target lives.
 */
constexpr float PriorityAddReach = 30.0f;

// Whether the bot may put its own add in "prioritized targets": the list is empty, holds the add it
// set itself, or only targets that are gone.
bool OwnsPrioritizedTargets(PlayerbotAI* botAI)
{
    auto const* context = static_cast<CoaAiObjectContext const*>(botAI->GetAiObjectContext());
    GuidVector const list = botAI->GetAiObjectContext()->GetValue<GuidVector>("prioritized targets")->Get();
    if (list.empty() || (list.size() == 1 && list.front() == context->priorityAdd))
        return true;
    return std::none_of(list.begin(), list.end(), [botAI](ObjectGuid guid)
    {
        Unit* unit = botAI->GetUnit(guid);
        return unit && unit->IsAlive();
    });
}

// The add to kill first: the bot's current target if it is one, else the nearest within reach. Only
// creatures in the group's fight count: on a group member's threat list, or summoned by one that is.
Unit* PriorityAdd(PlayerbotAI* botAI, Player* bot)
{
    if (!sPlayerbotAIConfig.coaDungeonPriorities || !bot->IsInCombat() || !bot->GetMap()->IsDungeon() ||
        GetCoaRole(bot) != CoaRole::Dps || !bot->GetGroup())
        return nullptr;

    AiObjectContext* const context = botAI->GetAiObjectContext();
    GuidVector const attackerList = context->GetValue<GuidVector>("attackers")->Get();
    if (attackerList.empty())
        return nullptr;
    std::unordered_set<ObjectGuid> const attackers(attackerList.begin(), attackerList.end());

    Unit* const current = context->GetValue<Unit*>("current target")->Get();
    Unit* best = nullptr;
    float bestDistance = PriorityAddReach;
    for (ObjectGuid const guid : context->GetValue<GuidVector>("possible targets")->Get())
    {
        Unit* unit = botAI->GetUnit(guid);
        Creature* creature = unit ? unit->ToCreature() : nullptr;
        if (!creature || !creature->IsAlive() || !bot->IsWithinDistInMap(creature, PriorityAddReach))
            continue;

        bool fighting = attackers.count(creature->GetGUID()) != 0;
        if (!fighting)
        {
            ObjectGuid summoner = creature->GetOwnerGUID();
            if (summoner.IsEmpty())
                summoner = creature->GetCreatorGUID();
            if (summoner.IsEmpty())
                if (TempSummon const* summon = creature->ToTempSummon())
                    summoner = summon->GetSummonerGUID();
            fighting = !summoner.IsEmpty() && attackers.count(summoner) != 0;
        }
        if (!fighting || !CoaDungeonPriorities::HealsOrProtects(creature))
            continue;

        if (creature == current)
            return creature;
        float const distance = bot->GetDistance(creature);
        if (distance < bestDistance)
        {
            best = creature;
            bestDistance = distance;
        }
    }
    return best;
}

class CoaPriorityAddTrigger : public Trigger
{
public:
    CoaPriorityAddTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa priority add", 500) {}

    bool IsActive() override
    {
        if (!sPlayerbotAIConfig.coaDungeonPriorities || !bot->IsInCombat() || !bot->GetMap()->IsDungeon())
            return false;
        Unit* add = PriorityAdd(botAI, bot);
        return add && add != AI_VALUE(Unit*, "current target") && OwnsPrioritizedTargets(botAI);
    }
};

class CoaPriorityAddAction : public AttackAction
{
public:
    CoaPriorityAddAction(PlayerbotAI* botAI) : AttackAction(botAI, "coa priority add") {}

    bool Execute(Event /*event*/) override
    {
        Unit* add = PriorityAdd(botAI, bot);
        if (!add || !OwnsPrioritizedTargets(botAI))
            return false;

        Unit* const previous = AI_VALUE(Unit*, "current target");
        context->GetValue<GuidVector>("prioritized targets")->Set({ add->GetGUID() });
        static_cast<CoaAiObjectContext*>(context)->priorityAdd = add->GetGUID();
        if (!Attack(add))
            return false;

        if (CoaDungeonPriorities::LogDue(CoaDungeonPriorities::Decision::Add))
            LOG_INFO("playerbots", "coa dungeon: {} switched from {} to {} ({}{})", bot->GetName(),
                     previous ? previous->GetName() : "nothing", add->GetName(), add->GetEntry(),
                     add->IsTotem() ? ", totem" : "");
        return true;
    }
};

// In a dungeon fight, a boss or elite near the bot carries a buff or an enrage one of its ready
// purges takes off. Checked once a second: the scan reads the bot's abilities.
class CoaEnemyBuffTrigger : public Trigger
{
public:
    CoaEnemyBuffTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa enemy buff", 1000) {}

    bool IsActive() override
    {
        bool const arena = sPlayerbotAIConfig.coaArenaPurge && bot->InArena();
        if (!bot->IsInCombat() || !ClassPurges(bot) ||
            (!arena && (!sPlayerbotAIConfig.coaDungeonPriorities || !bot->GetMap()->IsDungeon())))
            return false;
        return FindPurge(botAI, bot, KnownPurges(bot)).enemy != nullptr;
    }
};

// An enemy near the bot is casting something that can be interrupted.
class CoaEnemyCastingTrigger : public Trigger
{
public:
    CoaEnemyCastingTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa enemy casting") {}

    bool IsActive() override
    {
        // The kick first: FindCaster reports the casts it sees to the group (MayKick), which only
        // counts those a bot with a kick ready saw.
        return ClassHas(bot, KIND_INTERRUPT) &&
               HasReadyAbility(botAI, bot,[](uint16 kind) { return (kind & KIND_INTERRUPT) != 0; }) &&
               FindCaster(botAI, bot);
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
        // "melee" starts the white swings again at every tick and says it makes no threat: held like an attack, or the
        // swings stopped past the tank's threat came straight back (a Felsworn Slayer still reached 111%, 10/10).
        bool const swing = action && action->getName() == "melee";
        if (!sPlayerbotAIConfig.coaSmartTank || !action ||
            (action->getThreatType() == Action::ActionThreatType::None && !swing) || GetCoaRole(bot) != CoaRole::Dps)
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
        if (!hold)
            return 1.0f;
        // Aggro passes at 110% of the tank's threat in melee and 130% at range: a ranged bot holds 20 points later.
        float limit = float(hold + (botAI->IsRanged(bot) ? 20 : 0)) / 100.0f;
        // The tank's first seconds on a target: its threat is still small and the first big blows passed it before the
        // hold could see them coming (a Venomancer lost the boss at 3, 6 and 14 seconds, bench, 10/10). Players let the
        // tank settle: 60% of the limit for 3 seconds after its first touch.
        {
            uint32 const nowMs = getMSTime();
            if (settles.size() > 32)
                settles.clear();
            auto const touched = settles.emplace(target->GetGUID(), nowMs).first;
            if (getMSTimeDiff(touched->second, nowMs) < 3 * IN_MILLISECONDS)
                limit *= 0.6f;
        }
        // A mob that already left the tank has nothing left to hold: the tank taunts it back.
        auto const heldByTank = [](Unit* unit)
        {
            Unit* victim = unit->GetVictim();
            return victim && victim->IsPlayer() && PlayerbotAI::IsTank(victim->ToPlayer());
        };
        // How fast the share climbs: a melee bot at 4,000 to 7,000 per second went from under the limit to past the
        // 110% that takes aggro in a few blows (Guardian Gladiator, Templar Zealot, bench, 10/10). It holds as well when
        // the share would pass 105% in melee, 125% at range, within a second and a half.
        uint32 const now = getMSTime();
        if (trends.size() > 32)
            trends.clear();
        Trend& trend = trends[target->GetGUID()];
        uint32 const since = trend.at ? getMSTimeDiff(trend.at, now) : 0;
        if (since >= 500)
        {
            trend.rate = since < 5000 ? (share - trend.share) * 1000.0f / float(since) : 0.0f;
            trend.share = share;
            trend.at = now;
        }
        else if (!trend.at)
        {
            trend.share = share;
            trend.at = now;
        }
        float const predicted = share + std::max(0.0f, trend.rate) * 1.5f;
        if (heldByTank(target) && (share >= limit || predicted >= limit + 0.15f))
        {
            // White swings go on with no action at all: a Knight of Xoroth War held its spells and still took the
            // boss 7 times in 3 minutes from a Tyrant tank (bench, 10/10). Past the tank's threat a melee bot stops
            // swinging too; "coa attack" swings again once the tank is ahead.
            if ((share >= limit + 0.1f || predicted >= limit + 0.15f) && bot->GetVictim() == target && !botAI->IsRanged(bot))
                bot->AttackStop();
            return 0.0f;
        }
        // An area attack hits the whole pack: it waits until the tank holds every mob around the target, and holds
        // them by enough. An add the tank had not picked up yet took the damage dealers' area attacks and left for
        // them: adds stayed 12 to 27% of the time on a Felsworn Tyrant (bench, 10/10).
        auto const groupHasTank = [this]()
        {
            if (Group* group = bot->GetGroup())
                for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                    if (Player* member = ref->GetSource(); member && member != bot && member->IsAlive() &&
                                                            OnSameInstance(bot, member) && PlayerbotAI::IsTank(member))
                        return true;
            return false;
        };
        if (action->getThreatType() == Action::ActionThreatType::Aoe && groupHasTank())
            for (ObjectGuid const guid : AI_VALUE(GuidVector, "attackers"))
                if (Unit* unit = botAI->GetUnit(guid); unit && unit != target && unit->IsAlive() &&
                                                       unit->IsWithinDist(target, 10.0f) &&
                                                       (!heldByTank(unit) || ThreatShare(bot, unit) >= limit))
                    return 0.0f;
        return 1.0f;
    }

private:
    struct Trend
    {
        float share = 0.0f;
        float rate = 0.0f;  // share gained per second
        uint32 at = 0;
    };

    std::unordered_map<ObjectGuid, uint32> openers;  // target -> first seen untouched by a tank
    std::unordered_map<ObjectGuid, Trend> trends;    // target -> how fast the bot's share of the tank's threat climbs
    std::unordered_map<ObjectGuid, uint32> settles;  // target -> first seen touched by a tank
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
        triggers.push_back(new TriggerNode("coa dispel control", { NextAction("coa dispel", DispelControlPriority()) }));
        // A step out of a marked member's reach comes before any cast, as "avoid aoe" does.
        triggers.push_back(new TriggerNode("coa marked ally", { NextAction("coa avoid marked ally", ACTION_EMERGENCY + 4) }));
        // Breaking a stun or a fear comes before anything the bot could cast once free.
        triggers.push_back(new TriggerNode("coa pvp trinket", { NextAction("coa pvp trinket", ACTION_EMERGENCY + 6) }));
        triggers.push_back(new TriggerNode("coa enemy buff", { NextAction("coa purge", PurgePriority()) }));
        // Just above "dps assist" (50), which would otherwise turn the bot back to the boss first.
        triggers.push_back(new TriggerNode("coa priority add", { NextAction("coa priority add", ACTION_DISPEL + 1) }));
        triggers.push_back(new TriggerNode("medium aoe", { NextAction("coa aoe", ACTION_HIGH + 2) }));
        triggers.push_back(new TriggerNode("low health", { NextAction("coa defensive", ACTION_HIGH + 8) }));
        // A pet summoned by a player spell starts passive (CharmInfo) and never assists its owner:
        // the Stormbringer's Air Elemental did 0 damage and the Barbarian's Honored Ancestor never
        // swung on the boss bench. Same stance step as the hunter and warlock strategies.
        triggers.push_back(new TriggerNode("new pet", { NextAction("set pet stance", 60.0f) }));
    }

protected:
    virtual float InterruptPriority() { return ACTION_INTERRUPT; }
    virtual float DispelPriority() { return ACTION_NORMAL + 5; }
    // A controlled member: just below the purge, above the attacks.
    virtual float DispelControlPriority() { return ACTION_INTERRUPT - 2; }
    // Just below the interrupt: an enrage or a shield left on the boss costs more than one attack.
    virtual float PurgePriority() { return ACTION_INTERRUPT - 1; }

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
        // Above the spec rotations, whose attacks reach 88: at ACTION_HIGH + 4 it never fired (the NUC
        // arenas showed no change in taunts, 29/09). Only the rotation's own "lose aggro" taunt (94) and
        // its buffs (95) come first.
        triggers.push_back(new TriggerNode("coa loose enemy", { NextAction("coa taunt", 93.0f) }));
        // Area attacks stay under the spec rotation: raised to 86, they took the global cooldowns of the single target
        // threat abilities, and adds stayed on the tank 24 to 45% of the time instead of 39 to 71% (bench, 10/10).
        triggers.push_back(new TriggerNode("light aoe", { NextAction("coa aoe", ACTION_HIGH + 3) }));
        // Its defensive cooldowns when hurt, before its attacks: at ACTION_HIGH + 6 they lost to the spec rotation too.
        triggers.push_back(new TriggerNode("medium health", { NextAction("coa defensive", 89.0f) }));
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
        // The spec rotations put their attacks and buffs up to 30: the heals that act before a member
        // drops low sit just above them, or they never came (NUC dungeon arenas, 28/09: healers ended
        // their fights with 64-83% mana while members spent 4.6 s under half health before dying).
        triggers.push_back(new TriggerNode("party member medium health",
                                           { NextAction("coa hot", ACTION_CRITICAL_HEAL + 1),
                                             NextAction("coa heal", ACTION_CRITICAL_HEAL + 0.7f) }));
        triggers.push_back(new TriggerNode("party member almost full health",
                                           { NextAction("coa hot", ACTION_CRITICAL_HEAL + 0.6f) }));

        // Smart healing (AiPlayerbot.CoaSmartHeal; these triggers stay silent without it): a heal
        // over time kept on the tank, back within reach of the tank between casts - above the
        // attacks, below every heal - and a word to the group when the mana runs out.
        triggers.push_back(new TriggerNode("coa tank needs hot", { NextAction("coa hot", ACTION_CRITICAL_HEAL + 0.4f) }));
        triggers.push_back(new TriggerNode("coa far from tank", { NextAction("coa stay near tank", ACTION_CRITICAL_HEAL + 0.3f) }));
        triggers.push_back(new TriggerNode("coa out of healing reach", { NextAction("coa stay near tank", ACTION_EMERGENCY + 5) }));
        triggers.push_back(new TriggerNode("coa healer low mana", { NextAction("coa say low mana", ACTION_MEDIUM_HEAL + 8) }));
    }

protected:
    float InterruptPriority() override { return ACTION_MEDIUM_HEAL + 7; }
    float DispelPriority() override { return ACTION_MEDIUM_HEAL + 6; }
    // Above the heals of a member at medium health, below those of a member at low health.
    float DispelControlPriority() override { return ACTION_CRITICAL_HEAL + 1.5f; }
    float PurgePriority() override { return ACTION_MEDIUM_HEAL + 5; }
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
        // Never an NPC the player would not fight, nor one that talks or gives quests: an Alliance tank
        // in a Horde player's group pulled Deathstalker Adamant, the prisoner who opens Shadowfang Keep
        // (jealous-sound/azerothcore-wotlk-coa#5552, #5395).
        if (!creature->IsHostileTo(master) || creature->HasNpcFlag(NPCFlags(UNIT_NPC_FLAG_GOSSIP | UNIT_NPC_FLAG_QUESTGIVER)))
            continue;

        // From the tank or the player, whichever is nearer: a tank that stays put while the player stands (#6505)
        // was often behind the player, and the next pack lay out of its own 30 yards.
        float const distance = std::min(bot->GetDistance(creature), master->GetDistance(creature));
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
        // A Manastorm floor is one room with its boss: the tank pulled the boss across it on its own (17/10).
        if (bot->GetScriptedPrivateMapId() != MAPID_INVALID)
            return false;
        Player* master = GroupPlayer(botAI, bot);
        if (!master || !OnSameInstance(bot, master))
            return false;
        CoaAiObjectContext* context = static_cast<CoaAiObjectContext*>(botAI->GetAiObjectContext());
        time_t const now = time(nullptr);
        if (now - context->lastAutoPull < AutoPullPause)
            return false;
        bool const ready = GroupReadyToPull(bot);
        bool const target = ready && NextPull(botAI, bot, master);
        // Why the tank waits, once every 30 seconds at most: the group (someone dead, fighting, sitting, under 70%
        // health, a healer under 70% mana) or no target (nothing hostile within 30 yards, 40 from the player, in sight).
        if (!target && now - context->autoPullWaitLogged >= 30)
        {
            context->autoPullWaitLogged = now;
            LOG_INFO("playerbots", "coa auto pull: {} waits: {}", bot->GetName(), ready ? "no target in reach" : "group not ready");
        }
        return target;
    }
};

class CoaAutoPullAction : public AttackAction
{
public:
    CoaAutoPullAction(PlayerbotAI* botAI) : AttackAction(botAI, "coa auto pull") {}

    bool Execute(Event /*event*/) override
    {
        Player* master = GroupPlayer(botAI, bot);
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

// A corpse one bot goes to raise is left to it for a while: the tank of a Sunken Temple wipe got four
// resurrection casts from three bots while a dead damage dealer stayed down 18 seconds longer (headless runs, 12/10).
constexpr uint32 ResurrectClaimMs = 12 * IN_MILLISECONDS;
std::mutex ResurrectClaimsLock;
std::unordered_map<ObjectGuid, std::pair<ObjectGuid, uint32>> ResurrectClaims;  // dead -> (bot, until)

bool ClaimedByOther(Player* bot, Player* dead)
{
    std::lock_guard<std::mutex> guard(ResurrectClaimsLock);
    auto const found = ResurrectClaims.find(dead->GetGUID());
    return found != ResurrectClaims.end() && found->second.first != bot->GetGUID() &&
           !Passed(getMSTime(), found->second.second);
}

void ClaimCorpse(Player* bot, Player* dead)
{
    std::lock_guard<std::mutex> guard(ResurrectClaimsLock);
    uint32 const now = getMSTime();
    for (auto itr = ResurrectClaims.begin(); itr != ResurrectClaims.end();)
        itr = Passed(now, itr->second.second) ? ResurrectClaims.erase(itr) : std::next(itr);
    ResurrectClaims[dead->GetGUID()] = { bot->GetGUID(), now + ResurrectClaimMs };
}

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
        if (!dead || dead == bot || dead->IsAlive() || dead->HasPlayerFlag(PLAYER_FLAGS_GHOST) || dead->isResurrectRequested() ||
            !OnSameInstance(bot, dead) || ClaimedByOther(bot, dead))
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
        ClaimCorpse(bot, dead);

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
        // In a battleground the bot plays its objective; teleporting it to the player broke flag runs.
        bool const lost = master && !GET_PLAYERBOT_AI(master) && !bot->InBattleground() && bot->IsAlive() &&
                         master->IsAlive() && !bot->IsInCombat() && !master->IsInCombat() && !master->IsInFlight() &&
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
        // And it closes the distance while the group fights: too far to heal, it would never be in a
        // fight itself, so nothing else would ever bring it back within reach of the tank.
        triggers.push_back(new TriggerNode("coa far from tank", { NextAction("coa reach healing range", ACTION_CRITICAL_HEAL - 1) }));
        // Lost the player on the way: join them.
        triggers.push_back(new TriggerNode("coa lost the player", { NextAction("coa catch up", ACTION_HIGH + 5) }));
        // A dead group member is brought back once the group is out of the fight, before anything else.
        triggers.push_back(new TriggerNode("coa group member dead", { NextAction("coa resurrect", ACTION_CRITICAL_HEAL + 5) }));
    }
};

// The base out-of-combat strategy every WotLK class registers as "nc": its timer checks the mount
// state (mounting up with the master, getting off). The CoA classes had none, so "mount" was listed
// but nothing ever asked, and bots walked beside a mounted player (found in game, 27/09).
class CoaNonCombatStrategy : public NonCombatStrategy
{
public:
    CoaNonCombatStrategy(PlayerbotAI* botAI) : NonCombatStrategy(botAI) {}

    std::string const getName() override { return "nc"; }
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
    }

private:
    static Strategy* coa(PlayerbotAI* botAI) { return new CoaCombatStrategy(botAI); }
    static Strategy* coa_ranged(PlayerbotAI* botAI) { return new CoaCombatStrategy(botAI, true); }
    static Strategy* coa_tank(PlayerbotAI* botAI) { return new CoaTankStrategy(botAI); }
    static Strategy* coa_heal(PlayerbotAI* botAI) { return new CoaHealStrategy(botAI); }
};

// The out-of-combat strategies, which a bot holds together. In the factory above their names were
// siblings of one another: adding "coa auto pull" took "coa buff" off every tank, and "nc" then took
// the auto pull off (a dungeon finder tank never pulled, 27/09).
class CoaNonCombatStrategyFactoryInternal : public NamedObjectContext<Strategy>
{
public:
    CoaNonCombatStrategyFactoryInternal() : NamedObjectContext<Strategy>(false, false)
    {
        creators["coa buff"] = &CoaNonCombatStrategyFactoryInternal::coa_buff;
        creators["coa auto pull"] = &CoaNonCombatStrategyFactoryInternal::coa_auto_pull;
        creators["nc"] = &CoaNonCombatStrategyFactoryInternal::nc;
        // Here, not with the role strategies above: those are siblings, and adding one removes the
        // others - registered there, "coa boss dodge" took "coa" itself away from every bot (01/10).
        creators["coa boss dodge"] = &NewCoaBossDodgeStrategy;
    }

private:
    static Strategy* coa_auto_pull(PlayerbotAI* botAI) { return new CoaAutoPullStrategy(botAI); }
    static Strategy* coa_buff(PlayerbotAI* botAI) { return new CoaBuffStrategy(botAI); }
    static Strategy* nc(PlayerbotAI* botAI) { return new CoaNonCombatStrategy(botAI); }
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
        creators["coa avoid marked ally"] = &CoaActionFactoryInternal::coa_avoid_marked_ally;
        creators["coa pvp trinket"] = &CoaActionFactoryInternal::coa_pvp_trinket;
        creators["coa purge"] = &CoaActionFactoryInternal::coa_purge;
        creators["coa priority add"] = &CoaActionFactoryInternal::coa_priority_add;
        creators["coa interrupt"] = &CoaActionFactoryInternal::coa_interrupt;
        creators["coa buff"] = &CoaActionFactoryInternal::coa_buff;
        creators["coa stay near tank"] = &CoaActionFactoryInternal::coa_stay_near_tank;
        creators["coa reach healing range"] = &CoaActionFactoryInternal::coa_reach_healing_range;
        creators["coa say low mana"] = &CoaActionFactoryInternal::coa_say_low_mana;
        creators["coa auto pull"] = &CoaActionFactoryInternal::coa_auto_pull;
        creators["coa resurrect"] = &CoaActionFactoryInternal::coa_resurrect;
        creators["coa catch up"] = &CoaActionFactoryInternal::coa_catch_up;
        creators["coa boss dodge"] = &NewCoaBossDodgeAction;
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
    static Action* coa_avoid_marked_ally(PlayerbotAI* botAI) { return new CoaAvoidMarkedAllyAction(botAI); }
    static Action* coa_pvp_trinket(PlayerbotAI* botAI) { return new CoaPvpTrinketAction(botAI); }
    static Action* coa_purge(PlayerbotAI* botAI) { return new CoaPurgeAction(botAI); }
    static Action* coa_priority_add(PlayerbotAI* botAI) { return new CoaPriorityAddAction(botAI); }
    static Action* coa_interrupt(PlayerbotAI* botAI) { return new CoaInterruptAction(botAI); }
    static Action* coa_buff(PlayerbotAI* botAI) { return new CoaBuffAction(botAI); }
    static Action* coa_stay_near_tank(PlayerbotAI* botAI) { return new CoaStayNearTankAction(botAI); }
    static Action* coa_reach_healing_range(PlayerbotAI* botAI) { return new CoaReachHealingRangeAction(botAI); }
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
        creators["coa dispel control"] = &CoaTriggerFactoryInternal::coa_dispel_control;
        creators["coa marked ally"] = &CoaTriggerFactoryInternal::coa_marked_ally;
        creators["coa pvp trinket"] = &CoaTriggerFactoryInternal::coa_pvp_trinket;
        creators["coa enemy casting"] = &CoaTriggerFactoryInternal::coa_enemy_casting;
        creators["coa enemy buff"] = &CoaTriggerFactoryInternal::coa_enemy_buff;
        creators["coa priority add"] = &CoaTriggerFactoryInternal::coa_priority_add;
        creators["coa tank needs hot"] = &CoaTriggerFactoryInternal::coa_tank_needs_hot;
        creators["coa group member dropping"] = &CoaTriggerFactoryInternal::coa_group_member_dropping;
        creators["coa ready to pull"] = &CoaTriggerFactoryInternal::coa_ready_to_pull;
        creators["coa group member dead"] = &CoaTriggerFactoryInternal::coa_group_member_dead;
        creators["coa lost the player"] = &CoaTriggerFactoryInternal::coa_lost_the_player;
        creators["coa far from tank"] = &CoaTriggerFactoryInternal::coa_far_from_tank;
        creators["coa out of healing reach"] = &CoaTriggerFactoryInternal::coa_out_of_healing_reach;
        creators["coa healer low mana"] = &CoaTriggerFactoryInternal::coa_healer_low_mana;
        creators["coa loose enemy"] = &CoaTriggerFactoryInternal::coa_loose_enemy;
        creators["coa boss danger"] = &NewCoaBossDangerTrigger;
    }

private:
    static Trigger* coa_dispel(PlayerbotAI* botAI) { return new CoaDispelTrigger(botAI); }
    static Trigger* coa_dispel_control(PlayerbotAI* botAI) { return new CoaDispelControlTrigger(botAI); }
    static Trigger* coa_marked_ally(PlayerbotAI* botAI) { return new CoaMarkedAllyTrigger(botAI); }
    static Trigger* coa_pvp_trinket(PlayerbotAI* botAI) { return new CoaPvpTrinketTrigger(botAI); }
    static Trigger* coa_enemy_casting(PlayerbotAI* botAI) { return new CoaEnemyCastingTrigger(botAI); }
    static Trigger* coa_enemy_buff(PlayerbotAI* botAI) { return new CoaEnemyBuffTrigger(botAI); }
    static Trigger* coa_priority_add(PlayerbotAI* botAI) { return new CoaPriorityAddTrigger(botAI); }
    static Trigger* coa_tank_needs_hot(PlayerbotAI* botAI) { return new CoaTankNeedsHotTrigger(botAI); }
    static Trigger* coa_group_member_dropping(PlayerbotAI* botAI) { return new CoaGroupMemberDroppingTrigger(botAI); }
    static Trigger* coa_ready_to_pull(PlayerbotAI* botAI) { return new CoaReadyToPullTrigger(botAI); }
    static Trigger* coa_group_member_dead(PlayerbotAI* botAI) { return new CoaGroupMemberDeadTrigger(botAI); }
    static Trigger* coa_lost_the_player(PlayerbotAI* botAI) { return new CoaLostThePlayerTrigger(botAI); }
    static Trigger* coa_far_from_tank(PlayerbotAI* botAI) { return new CoaFarFromTankTrigger(botAI); }
    static Trigger* coa_out_of_healing_reach(PlayerbotAI* botAI) { return new CoaOutOfHealingReachTrigger(botAI); }
    static Trigger* coa_healer_low_mana(PlayerbotAI* botAI) { return new CoaLowManaTrigger(botAI); }
    static Trigger* coa_loose_enemy(PlayerbotAI* botAI) { return new CoaLooseEnemyTrigger(botAI); }
};

}  // namespace

void ForgetWildcardKit(Player const* bot)
{
    std::lock_guard<std::mutex> lock(WildcardKitLock);
    WildcardKits.erase(bot->GetGUID().GetCounter());
}

bool CoaBotCasts(SpellInfo const* info)
{
    CoaAbility ability = { info->Id, 0, 0, 0, info->GetFirstRankSpell()->Id };
    Classify(info, ability);
    return ability.kind != 0;
}

// CoaPvpTactics.cpp: crowd control on an enemy player, when the bot knows one that is ready.
bool CoaHasReadyControl(PlayerbotAI* botAI, Player* bot)
{
    return ClassHas(bot, KIND_CONTROL) && HasReadyAbility(botAI, bot, [](uint16 kind) { return (kind & KIND_CONTROL) != 0; });
}

bool CoaCastControl(PlayerbotAI* botAI, Player* bot, Unit* target)
{
    return CastFirst(botAI, bot, KnownAbilities(bot, [](uint16 kind) { return (kind & KIND_CONTROL) != 0; }), target) != nullptr;
}

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

    std::shared_ptr<ClassKit const> const kit = KitOf(bot);
    if (!kit)
        return false;
    uint32 const first = info->GetFirstRankSpell()->Id;
    for (CoaAbility const& ability : kit->abilities)
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

// Whether the spell, or a spell it casts, forces its target to attack the caster.
static bool Taunts(SpellInfo const* info, uint8 depth)
{
    for (SpellEffectInfo const& effect : info->Effects)
    {
        if (effect.Effect == SPELL_EFFECT_ATTACK_ME || (effect.IsAura() && effect.ApplyAuraName == SPELL_AURA_MOD_TAUNT))
            return true;
        if (depth < 2 && effect.TriggerSpell &&
            (effect.Effect == SPELL_EFFECT_TRIGGER_SPELL || effect.Effect == SPELL_EFFECT_TRIGGER_MISSILE ||
             effect.Effect == SPELL_EFFECT_TRIGGER_SPELL_WITH_VALUE))
            if (SpellInfo const* triggered = sSpellMgr->GetSpellInfo(effect.TriggerSpell))
                if (triggered != info && Taunts(triggered, depth + 1))
                    return true;
    }
    return false;
}

bool CoaTankOnlySpell(Player* bot, SpellInfo const* info, Unit* target)
{
    // Every taunt of the spell store, read from its effects: the class kits knew 28 of them, and a taunt cast
    // by a damage spell, swapped in at run time or learned outside the kit went through (Dogsmeller, 10/10:
    // damage dealers taunting in mythic+).
    static std::unordered_set<uint32> const taunts = []
    {
        std::unordered_set<uint32> ids;
        for (uint32 id = 1; id < sSpellMgr->GetSpellInfoStoreSize(); ++id)
            if (SpellInfo const* spell = sSpellMgr->GetSpellInfo(id))
                if (Taunts(spell, 0))
                    ids.insert(id);
        for (auto const& [classId, kit] : ClassAbilities())
            for (CoaAbility const& ability : kit.abilities)
                if (ability.kind & KIND_TAUNT)
                    ids.insert({ ability.spellId, ability.firstSpellId });
        LOG_INFO("playerbots", "CoA tank only: {} taunts in the spell store", ids.size());
        return ids;
    }();

    if (!info || !bot)
        return false;
    uint32 const first = info->GetFirstRankSpell()->Id;
    bool const taunt = taunts.count(info->Id) || taunts.count(first);
    // A tank does not taunt an enemy already hitting it with a taunt that deals no damage, whatever asks for it (its
    // spec rotation listed Betray among its attacks): the taunt stays ready for the add that runs to a healer.
    if (GetCoaRole(bot) == CoaRole::Tank)
        return taunt && target && target != bot && target->GetVictim() == bot && !DealsDamage(info);
    // A spell of the bot's own raising its threat (a tank stance, Righteous Fury), kit or not.
    bool const threat = !taunt && (!target || target == bot) && RaisesThreat(info);
    if (taunt || threat)
    {
        static std::mutex logged;
        static std::unordered_set<uint32> seen;
        std::lock_guard<std::mutex> guard(logged);
        if (seen.insert(info->Id).second)
            LOG_INFO("playerbots", "coa tank only: {} (class {}) kept from {} {} ({})", bot->GetName(), bot->getClass(),
                     taunt ? "taunt" : "threat buff", info->SpellName[LOCALE_enUS], info->Id);
    }
    return taunt || threat;
}

void CoaTankThreatBonus(Player* bot)
{
    // Increased Threat: passive, hidden, permanent, threat of every school raised. Its amount is the option's, set
    // again when the option changes; a bot that is no tank any more loses it.
    static uint32 constexpr increasedThreat = 35773;
    uint32 const bonus = sPlayerbotAIConfig.coaTankThreatBonus;
    bool const wanted = bonus && GetCoaRole(bot) == CoaRole::Tank;
    Aura* aura = bot->GetAura(increasedThreat, bot->GetGUID());
    if (!wanted)
    {
        if (aura)
            bot->RemoveAura(aura);
        return;
    }
    if (!aura)
        aura = bot->AddAura(increasedThreat, bot);
    if (AuraEffect* effect = aura ? aura->GetEffect(EFFECT_0) : nullptr; effect && effect->GetAmount() != int32(bonus))
        effect->ChangeAmount(int32(bonus));
}

void CoaTankThreatBuffs(PlayerbotAI* botAI, Player* bot)
{
    if (!botAI || !bot || GetCoaRole(bot) != CoaRole::Tank || bot->IsInCombat() || bot->IsMounted())
        return;

    // The tank's own threat buffs that its class kit does not list (learned from a talent, an upgrade, swapped in):
    // the buff action knew only the kit, and tanks were seen holding little aggro. Stances stay with the buff action,
    // which knows which of them is the tank's; a buff of a family the tank already wears is left alone.
    static std::mutex logged;
    static std::unordered_set<uint64> seen;
    bool wears = WearsThreatStance(bot);
    for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
    {
        if (playerSpell->State == PLAYERSPELL_REMOVED || !playerSpell->Active)
            continue;
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
        if (!info || info->IsPassive() || !info->IsPositive() || info->HasAura(SPELL_AURA_MOD_SHAPESHIFT) ||
            info->RecoveryTime > 10 * IN_MILLISECONDS || (info->GetDuration() > 0 && info->GetDuration() < 10 * MINUTE * IN_MILLISECONDS) ||
            !RaisesThreat(info))
            continue;
        if (SpellInfo const* next = info->GetNextRankSpell(); next && bot->HasSpell(next->Id))
            continue;
        if (bot->HasAura(spellId) || StandsIn(bot, info) || !botAI->CanCastSpell(spellId, bot))
            continue;
        if (botAI->CastSpell(spellId, bot))
        {
            wears = true;
            std::lock_guard<std::mutex> guard(logged);
            if (seen.insert((uint64(bot->getClass()) << 32) | spellId).second)
                LOG_INFO("playerbots", "coa tank threat: {} (class {}) puts on {} ({})", bot->GetName(), bot->getClass(),
                         info->SpellName[LOCALE_enUS], spellId);
            return;
        }
    }
    if (!wears)
    {
        std::lock_guard<std::mutex> guard(logged);
        if (seen.insert((uint64(1) << 63) | bot->GetGUID().GetCounter()).second)
            LOG_INFO("playerbots", "coa tank threat: {} (class {}, level {}) wears no threat aura", bot->GetName(),
                     bot->getClass(), bot->GetLevel());
    }
}

bool CoaRivalBuffBlocks(Player* bot, SpellInfo const* info, Unit* target)
{
    // A caster keeps one of its rival buffs on each target (Greater Devotions of a Sun Cleric, Seals, Adaptations):
    // a spec rotation asking for all four with "buff missing" put one on, which took the other off, and asked for
    // that one again: 75 Devotions in a 3 minute fight (bench, 10/10).
    // On the bot itself, CoaRotationMayCast lets the member its rotation keeps replace a sibling (stances, Tattoos,
    // permanent): only the buffs that run out are held there. A Knight of Xoroth tank cast its Greater Marks of
    // Korth'azz, Blaumeux and Zeliek, group buffs of 30 minutes, 42 times in a 3 minute fight (bench, 10/10).
    if (!bot || !info || !target || !info->IsPositive())
        return false;
    if (target == bot && (info->GetMaxDuration() <= 0 || info->HasAura(SPELL_AURA_MOD_SHAPESHIFT)))
        return false;
    return WearsRivalBuffFrom(target, bot, info) && !target->GetAura(info->Id, bot->GetGUID());
}

bool CoaCarriesBotAway(Player* bot, SpellInfo const* info)
{
    if (!bot || !info)
        return false;
    // A healer never jumps, leaps, charges or blinks, in a fight or out of it: a Flameweaving healer cast Dragon
    // Leap on cooldown from "coa attack" and "coa aoe" and landed in packs nobody had pulled (headless dungeon
    // runs, 11/10). No CoA healer path moves the bot on purpose with a spell; its feet do it.
    bool const healerMove = GetCoaRole(bot) == CoaRole::Heal && MovesCaster(info);
    if (!healerMove && !bot->IsInCombat())
        return false;
    // In a fight a bot neither rolls off for seconds nor rushes on past the enemy it stands at: Wildwalkers spent
    // a sixth of each boss fight out of reach after Boulder Dash and Primal Rush (bench, 10/10). Out of reach, a
    // rush ahead still closes the gap.
    Unit* const victim = bot->GetVictim();
    // A ranged damage dealer does not leap ahead into the fight it shoots at: a Rot Venomancer used Skitter (15 yards
    // forward) as a filler 114 times in Sunken Temple and fled back after 43 of them (headless runs, 12/10).
    bool const away = healerMove || ForcesCasterForward(info) ||
                      (SendsCasterAhead(info) && ((victim && bot->IsWithinMeleeRange(victim)) ||
                                                  PlayerbotAI::IsRanged(bot, true)));
    if (away)
    {
        static std::mutex logged;
        static std::unordered_set<uint32> seen;
        std::lock_guard<std::mutex> guard(logged);
        if (seen.insert(info->Id).second)
            LOG_INFO("playerbots", "coa carries away: {} (class {}) kept from {} ({}) in a fight", bot->GetName(),
                     bot->getClass(), info->SpellName[LOCALE_enUS], info->Id);
    }
    return away;
}

bool CoaRotationMayCast(PlayerbotAI* botAI, Player* bot, SpellInfo const* info)
{
    if (!info)
        return true;
    // Temporal Return, the Chronomancer's hearthstone, is in the three Chronomancer rotations: cast between
    // two packs, it sent a Time healer to Orgrimmar in the middle of Razorfen Kraul
    // (jealous-sound/azerothcore-wotlk-coa#6286). Befriend Beast, in the three Ranger rotations, possesses
    // a beast no bot can steer (#6111).
    if (TeleportsCasterHome(info) || PossessesTarget(info))
        return false;
    if (CoaHoldsExclusiveSibling(bot, info))
        return false;
    if (!sPlayerbotAIConfig.coaExclusiveFamilies)
        return true;

    // The member the rotation keeps replaces a sibling the bot wears from itself (one "coa buff" put on, or
    // one cast while the kept one was on cooldown), never one of another caster: two Reapers keeping two
    // Rites would take each other's off for ever. Any other member waits until the bot wears none of the
    // family.
    Aura const* const worn = WornExclusiveSibling(bot, bot, info);
    return !worn || (worn->GetCasterGUID() == bot->GetGUID() &&
                     RotationKeptMember(botAI, info) == info->GetFirstRankSpell()->Id);
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
    strategyContexts.Add(new CoaNonCombatStrategyFactoryInternal());
    strategyContexts.Add(NewCoaPvpStrategyContext());
}

void CoaAiObjectContext::BuildSharedActionContexts(SharedNamedObjectContextList<Action>& actionContexts)
{
    AiObjectContext::BuildSharedActionContexts(actionContexts);
    actionContexts.Add(new CoaActionFactoryInternal());
    actionContexts.Add(NewCoaPvpActionContext());
}

void CoaAiObjectContext::BuildSharedTriggerContexts(SharedNamedObjectContextList<Trigger>& triggerContexts)
{
    AiObjectContext::BuildSharedTriggerContexts(triggerContexts);
    triggerContexts.Add(new CoaTriggerFactoryInternal());
    triggerContexts.Add(NewCoaPvpTriggerContext());
}

void CoaAiObjectContext::BuildSharedValueContexts(SharedNamedObjectContextList<UntypedValue>& valueContexts)
{
    AiObjectContext::BuildSharedValueContexts(valueContexts);
}
