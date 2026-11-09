/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "Aq20Actions.h"
#include "Aq20Utils.h"
#include <cmath>

bool Aq20UseCrystalAction::Execute(Event /*event*/)
{
    if (Unit* boss = AI_VALUE2(Unit*, "find target", "ossirian the unscarred"))
    {
        if (GameObject* crystal = RaidAq20Utils::GetNearestCrystal(boss))
        {
            float botDist = bot->GetDistance(crystal);
            if (botDist > INTERACTION_DISTANCE)
                return MoveTo(bot->GetMapId(),
                    crystal->GetPositionX() + frand(-3.5f, 3.5f),
                    crystal->GetPositionY() + frand(-3.5f, 3.5f),
                    crystal->GetPositionZ());

            // if we're already in range just wait here until it's time to activate crystal
            SetNextMovementDelay(500);

            // don't activate crystal if boss too far or its already been activated
            if (boss->GetDistance(crystal) > 25.0f ||
                crystal->HasGameObjectFlag(GO_FLAG_IN_USE))
                return false;

            // don't activate crystal if boss doesn't have buff yet AND isn't going to have it soon
            // (though ideally bot should activate it ~5 seconds early due to time it takes for
            // crystal to activate and remove buff)
            if (!RaidAq20Utils::IsOssirianBuffActive(boss) &&
                RaidAq20Utils::GetOssirianDebuffTimeRemaining(boss) > 5000)
                return false;

            // this makes crystal do its animation (then disappear after)
            WorldPacket data1(CMSG_GAMEOBJ_USE);
            data1 << crystal->GetGUID();
            bot->GetSession()->HandleGameObjectUseOpcode(data1);

            // this makes crystal actually remove the buff and put on debuff (took a while to figure that out)
            WorldPacket data2(CMSG_GAMEOBJ_USE);
            data2 << crystal->GetGUID();
            bot->GetSession()->HandleGameobjectReportUse(data2);

            return true;
        }
    }
    return false;
}

// ---- CoA Combat (lot 7, docs/AQ-ETAT-3.0.md) ----

bool Aq20BuruAvoidDevastateAction::Execute(Event /*event*/)
{
    Unit* buru = bot->FindNearestCreature(CoaAq20::NPC_BURU, 40.0f);
    if (!buru)
        return false;
    return MoveAway(buru, CoaAq20::DEVASTATE_YARDS + 3.0f - bot->GetDistance2d(buru));
}

bool Aq20OssirianShelterAction::Execute(Event /*event*/)
{
    Unit* obelisk = RaidAq20Utils::GetNearestActiveObelisk(bot);
    if (!obelisk)
        return false;
    float const dx = bot->GetPositionX() - obelisk->GetPositionX();
    float const dy = bot->GetPositionY() - obelisk->GetPositionY();
    float const length = std::sqrt(dx * dx + dy * dy);
    float const keep = CoaAq20::OBELISK_KEEP_YARDS - 4.0f;
    float x = obelisk->GetPositionX();
    float y = obelisk->GetPositionY();
    if (length > 0.1f)
    {
        x += dx / length * keep;
        y += dy / length * keep;
    }
    return MoveTo(bot->GetMapId(), x, y, obelisk->GetPositionZ(), false, false, false, true,
                  MovementPriority::MOVEMENT_FORCED);
}
