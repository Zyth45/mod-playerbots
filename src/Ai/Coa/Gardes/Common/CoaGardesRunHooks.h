/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
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
