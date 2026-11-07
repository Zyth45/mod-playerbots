/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_TARGET_EXCLUSION_H
#define COA_TARGET_EXCLUSION_H

#include "CoaTargetExclusionTable.h"
#include "Define.h"

#include <functional>

class Player;

// The guard's name in CoaGroupTank::ModuleGuardActs: silent for the members of an active Dungeon Clear run while
// Dungeon Clear still runs its own copy (DungeonClearCombatStrategy::AppendTargetExclusions and the
// DungeonClearDps{,Aoe}TargetValue decorators).
inline constexpr char const* CoaTargetExclusionGuard = "target exclusion";

namespace CoaTargetExclusion
{
    // Two windows only a Dungeon Clear run can open, read through hooks Dungeon Clear sets at load (the module never
    // depends on Dungeon Clear). Unset, both answer false: no combat purge was armed and no boss was skipped, which is
    // exactly the state outside a run.
    struct RunHooks
    {
        // Has a combat purge just barred this creature entry for the bot's run? (DcCombatPurge::IsBarred)
        std::function<bool(Player* bot, uint32 entry)> purgeBarred;
        // Has the operator skipped this boss entry in the bot's run? (`dc skip`, DcKey::Skipped)
        std::function<bool(Player* bot, uint32 bossEntry)> bossSkipped;
    };

    // Set once, at load, before the maps update.
    void SetRunHooks(RunHooks hooks);

    // Is `entry` excluded for `bot` right now? Runs the row's live gate. `forTank` asks for a TANK pick, which only
    // `alsoTank` rows bar.
    bool IsExcluded(Player* bot, uint32 mapId, uint32 entry, bool forTank = false);
}

#endif
