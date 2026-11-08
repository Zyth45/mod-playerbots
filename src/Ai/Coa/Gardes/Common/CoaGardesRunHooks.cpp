/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "CoaGardesRunHooks.h"

namespace
{
    CoaGardesRunHooks::CampHoldPassive CampHook;
    CoaGardesRunHooks::RoomClearBoss RoomBossHook;
    CoaGardesRunHooks::RaidStandDown StandDownHook;
}

void CoaGardesRunHooks::SetCampHoldPassive(CampHoldPassive hook) { CampHook = std::move(hook); }
void CoaGardesRunHooks::SetRoomClearBoss(RoomClearBoss hook) { RoomBossHook = std::move(hook); }
void CoaGardesRunHooks::SetRaidStandDown(RaidStandDown hook) { StandDownHook = std::move(hook); }

bool CoaGardesRunHooks::IsCampHoldPassive(Player* member)
{
    return member && CampHook && CampHook(member);
}

Creature* CoaGardesRunHooks::RoomClearBossOf(Player* tank)
{
    return tank && RoomBossHook ? RoomBossHook(tank) : nullptr;
}

bool CoaGardesRunHooks::IsRaidStandDown(Player* member)
{
    return member && StandDownHook && StandDownHook(member);
}
