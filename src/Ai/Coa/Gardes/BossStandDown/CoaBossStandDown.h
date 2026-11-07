/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_BOSS_STAND_DOWN_H
#define COA_BOSS_STAND_DOWN_H

#include "CoaBossStandDownKernel.h"
#include "Define.h"
#include "Value.h"

#include <functional>

class Player;
class PlayerbotAI;

// The guard's name in CoaGroupTank::ModuleGuardActs: silent for the members of an active Dungeon Clear run while
// Dungeon Clear still evaluates its own stand-down (DcBossStandDown::IsActive on the leader's DcRunState).
inline constexpr char const* CoaBossStandDownGuard = "boss stand down";

// Raid boss stand-down, the live half (moved from Dungeon Clear's DcBossStandDown::IsActive). The verdict is the
// GROUP's: one state per group (per bot when ungrouped), mutex-guarded, evaluated at most once per tick window by
// whichever member asks first, and cleared when the group leaves the map. In-encounter when the instance script
// reports IsEncounterInProgress(), or when a roster boss holds any member on the reference tank's map in PvE combat
// (the no-script fallback); on a five-man table map, when the table boss is alive and in combat.
//
// False everywhere outside raid maps and the five-man table.
namespace CoaBossStandDown
{
    // Is `entry` a boss of the run? Dungeon Clear sets this at load to its roster (DungeonBosses, kind Boss), so the
    // fallback matches what the run is clearing. Unset, a creature counts when it is a dungeon or world boss.
    using RosterBossProvider = std::function<bool(Player* at, uint32 entry)>;
    void SetRosterBossProvider(RosterBossProvider provider);

    bool IsActive(Player* bot);
}

// "coa boss stand down": CoaBossStandDown::IsActive for this bot (not cached: the verdict has its own memo).
class CoaBossStandDownValue : public BoolCalculatedValue
{
public:
    CoaBossStandDownValue(PlayerbotAI* botAI) : BoolCalculatedValue(botAI, "coa boss stand down") {}
    bool Calculate() override;
};

#endif
