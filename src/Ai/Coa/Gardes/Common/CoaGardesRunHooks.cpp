/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
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
