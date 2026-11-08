/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COA_COMBAT_GROUP_TANK_H
#define PLAYERBOTS_COA_COMBAT_GROUP_TANK_H

class Player;

// The tank a bot's group fights behind, for the CoA Combat guards.
//
// In priority order, each candidate alive, in world and on the bot's map (otherwise the next one is tried):
//   1. the tank an autopilot names for the bot's run (RunTankProvider, set by an outside module such as Dungeon Clear);
//   2. a real player of the group who said "tank main on" (CoaPlayerMainTank);
//   3. the first group slot flagged main tank by the raid frame;
//   4. the first member, in group order, whose role is tank.
// A tank on another map (still outside, at the graveyard) does not count: the bot is not fighting behind it.
namespace CoaCombatGroupTank
{
    // Returns the run's tank for this bot, or nullptr when the bot is not in such a run. Called from the map threads.
    using RunTankProvider = Player* (*)(Player* bot);

    void SetRunTankProvider(RunTankProvider provider);

    Player* Of(Player* bot);
}

#endif
