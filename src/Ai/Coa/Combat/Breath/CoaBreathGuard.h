/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COA_BREATH_GUARD_H
#define PLAYERBOTS_COA_BREATH_GUARD_H

#include "Define.h"

class Player;

// Breath guard (CoA Combat, guard B2). Nothing else lets a bot know it can drown: a party held near a straggler or
// resting in Blackfathom Deeps' flooded passage (to Lady Sarevess) or the pool under Kelris lost a member every ~190 s.
//
// A grouped bot in a dungeon, out of combat, under water for half its breath bar (90 s) is brought back up beside its
// reference tank, or to that tank's last dry spot when the tank is under water too. It acts for every member of a
// Dungeon Clear run, and outside a run when AiPlayerbot.CoaCombat.BreathGuard.Enable is 1 (0 by default).
namespace CoaBreathGuard
{
    // Whether `bot` or a living member of its group on its map is under water. An autopilot's party gate asks it, so
    // that a party is never held under water waiting for a straggler or a rest.
    bool AnyMemberUnderWater(Player* bot);

    // World thread: advances every bot's clock by `diffMs` and brings up the ones past the limit.
    void Tick(uint32 diffMs);
}

#endif
