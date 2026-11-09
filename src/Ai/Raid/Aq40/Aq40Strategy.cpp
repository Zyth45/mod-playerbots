/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "Aq40Strategy.h"
#include "Aq40Helpers.h"
#include "Aq40Multipliers.h"
#include "Playerbots.h"

using namespace Aq40Helpers;

void RaidAq40Strategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // ----------- C'Thun (CoA) -----------

    triggers.push_back(new TriggerNode(
        "aq40 cthun eldritch revelations", { NextAction("aq40 cthun look away", ACTION_EMERGENCY + 6) }));

    triggers.push_back(new TriggerNode(
        "aq40 cthun dark glare", { NextAction("aq40 cthun dodge glare", ACTION_EMERGENCY + 5) }));

    triggers.push_back(new TriggerNode(
        "aq40 cthun leave stomach", { NextAction("aq40 cthun leave stomach", ACTION_EMERGENCY + 4) }));

    triggers.push_back(new TriggerNode(
        "aq40 cthun devour", { NextAction("aq40 cthun avoid devour", ACTION_EMERGENCY + 3) }));

    triggers.push_back(new TriggerNode(
        "aq40 cthun center", { NextAction("aq40 cthun spit out", ACTION_EMERGENCY + 2) }));

    triggers.push_back(new TriggerNode(
        "aq40 cthun target", { NextAction("aq40 cthun attack", ACTION_RAID + 1) }));

    triggers.push_back(new TriggerNode(
        "aq40 cthun position", { NextAction("aq40 cthun move to spot", ACTION_MOVE + 5) }));

    // ----------- The Prophet Skeram (CoA) -----------

    triggers.push_back(new TriggerNode(
        "aq40 skeram psychic shock", { NextAction("aq40 skeram avoid psychic shock", ACTION_EMERGENCY + 3) }));

    triggers.push_back(new TriggerNode(
        "aq40 skeram hold", { NextAction("aq40 skeram stop attack", ACTION_RAID + 2) }));

    triggers.push_back(new TriggerNode(
        "aq40 skeram target", { NextAction("aq40 skeram attack", ACTION_RAID + 1) }));

    // Below critical heals.
    triggers.push_back(new TriggerNode(
        "aq40 skeram gaze spread", { NextAction("aq40 skeram gaze spread", ACTION_HIGH + 5) }));

    // CoA Combat (lot 7, docs/AQ-ETAT-3.0.md): Sartura's Phantom Dancers first, and out of their blades (SA2).
    triggers.push_back(new TriggerNode(
        "aq40 sartura mark", { NextAction("aq40 sartura mark", ACTION_RAID) }));
    triggers.push_back(new TriggerNode(
        "aq40 sartura dancers", { NextAction("aq40 sartura avoid dancer", ACTION_EMERGENCY + 3) }));
    // CoA Combat: Viscidus frozen then shattered (VS1), the emperors apart (TW1), melee down with the prey (CT1).
    triggers.push_back(new TriggerNode(
        "aq40 viscidus frost", { NextAction("aq40 viscidus frost", ACTION_RAID + 1) }));
    triggers.push_back(new TriggerNode(
        "aq40 viscidus shatter", { NextAction("aq40 viscidus shatter", ACTION_RAID + 1) }));
    triggers.push_back(new TriggerNode(
        "aq40 twin emperors apart", { NextAction("aq40 twin emperors apart", ACTION_RAID + 2) }));
    triggers.push_back(new TriggerNode(
        "aq40 cthun join devour", { NextAction("aq40 cthun join devour", ACTION_EMERGENCY + 3) }));
}

void RaidAq40Strategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new Aq40CthunMultiplier(botAI));
    multipliers.push_back(new Aq40SkeramMultiplier(botAI));
    multipliers.push_back(new Aq40SarturaInterruptMultiplier(botAI));
}

void RaidAq40Strategy::AppendTargetExclusions(GuidSet& exclusions, TargetValueExclusionType type)
{
    Player* bot = botAI->GetBot();

    // Skeram and his images behind the Nightmare Veil, or while a worshipper
    // is too low to take a quarter of the damage.
    if (IsSkeramEncounter(bot))
    {
        bool const hold =
            type == TargetValueExclusionType::DpsTarget && !PlayerbotAI::IsTank(bot) && ShouldHoldOnSkeram(bot);
        if (hold || IsSkeramVeiled(bot))
            for (Creature* copy : GetSkeramUnits(bot, NPC_SKERAM))
                exclusions.insert(copy->GetGUID());
        return;
    }

    if (!IsCthunEncounter(bot))
        return;

    // C'Thun under his Impenetrable Force Shield is not worth a global cooldown.
    if (Creature* cthun = GetCthun(bot))
        if (!IsCthunWeakened(bot))
            exclusions.insert(cthun->GetGUID());

    if (type != TargetValueExclusionType::DpsTarget)
        return;

    // Phase 1: only the four Eye spots stand in the Eye's melee range.
    if (!IsCthunPhaseTwo(bot) && (PlayerbotAI::IsMelee(bot) || PlayerbotAI::IsTank(bot)) && !IsEyeMeleeSpot(bot))
        if (Creature* eye = GetEye(bot))
            exclusions.insert(eye->GetGUID());
}
