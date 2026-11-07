/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_HAZARD_VACATE_H
#define COA_HAZARD_VACATE_H

#include "MovementActions.h"
#include "Strategy.h"
#include "Trigger.h"

// The guard's name in CoaGroupTank::ModuleGuardActs: silent for the members of an active Dungeon Clear run while
// Dungeon Clear still runs its own copy (DungeonClearHazardVacate{Trigger,Action}).
inline constexpr char const* CoaGroundHazardsGuard = "ground hazards";

// ANY role, BOTH engines. Fires while the bot stands inside the pulse of an active-vacate hazard it cannot fight (the
// Destroyed Sentinel, a Creeping Sludge, a Cloud of Disease or Noxious Cloud pool, Gehennas' flame orb, Molten Core's
// Rain of Fire, a trap). No combat gate (these tick after the kill, often out of combat) and no role exemption
// (nothing to tank). Inert on maps with no rows, while stunned/rooted/feared/confused, and when the setting
// AiPlayerbot.HazardVacate (legacy DungeonClear.HazardVacate first) is off. Ported from Dungeon Clear 2.9.4
// (DungeonClearHazardVacateTrigger), logic unchanged.
class CoaHazardVacateTrigger : public Trigger
{
public:
    CoaHazardVacateTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa hazard vacate", 1) {}
    bool IsActive() override;
};

// Moves the bot OUT of the nearest active-vacate pulse: aims directly away from the emitter, past its pulse radius
// plus that row's retreat slack, snapped to the navmesh, clear of every hazard's placement keep-out AND of every live
// danger band, reachable by a path no longer than the detour bound; if the straight-away point fails it fans the
// bearing around. MOVEMENT_COMBAT priority so it overrides the chase. Commits to the chosen point (at most
// CoaHazard::VacateCommitMaxMs) so a field of emitters does not re-elect a centre every step and reverse the bearing.
// Ported from Dungeon Clear 2.9.4 (DungeonClearHazardVacateAction), logic unchanged.
class CoaHazardVacateAction : public MovementAction
{
public:
    CoaHazardVacateAction(PlayerbotAI* botAI) : MovementAction(botAI, "coa hazard vacate") {}
    bool Execute(Event event) override;

private:
    Position _fleeTo;
    bool _fleeToValid = false;
    uint32 _fleeSetAtMs = 0;
};

// The guard's strategy, for BOTH engines: the vacate rung at Dungeon Clear's relevance (55, above the movement and
// combat rungs, below emergencies).
class CoaGroundHazardsStrategy : public Strategy
{
public:
    CoaGroundHazardsStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}
    std::string const getName() override { return "coa ground hazards"; }
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
};

#endif
