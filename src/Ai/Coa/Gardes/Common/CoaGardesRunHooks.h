/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_GARDES_RUN_HOOKS_H
#define COA_GARDES_RUN_HOOKS_H

#include <functional>

class Creature;
class Player;

// What only a Dungeon Clear run knows, asked by the party combat guards while a member is in an active run
// (CoaGroupTank::InDungeonClearRun). Dungeon Clear sets them at load, before the maps update, and never changes them
// afterwards; the module never depends on Dungeon Clear. Left unset, every hook answers "nothing": the guards then
// behave as in a group without a run.
namespace CoaGardesRunHooks
{
    // The run holds the party PASSIVE at an advanced-pull camp (Forming / Advancing / Returning): the camp and assist
    // actions own the positioning there, and the combat regroup stands down. Dungeon Clear: GetLeaderCampHold passive.
    using CampHoldPassive = std::function<bool(Player* member)>;
    // The sleeping room-aggro boss whose room the run leader `tank` is clearing right now, or nullptr when no room clear
    // is active. Dungeon Clear: IsRoomClearActive + NextDungeonBoss + GetLiveBoss.
    using RoomClearBoss = std::function<Creature*(Player* tank)>;
    // A raid encounter's stand-down hands the fight to the playerbots raid strategy (Dungeon Clear: DcBossStandDown).
    using RaidStandDown = std::function<bool(Player* member)>;

    void SetCampHoldPassive(CampHoldPassive hook);
    void SetRoomClearBoss(RoomClearBoss hook);
    void SetRaidStandDown(RaidStandDown hook);

    bool IsCampHoldPassive(Player* member);
    Creature* RoomClearBossOf(Player* tank);
    bool IsRaidStandDown(Player* member);
}

#endif
