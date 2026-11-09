/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaCleanWorld.h"

#include "CellImpl.h"
#include "CoaCleanWorldRules.h"
#include "Corpse.h"
#include "Creature.h"
#include "GameTime.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Map.h"
#include "MotionMaster.h"
#include "NewRpgInfo.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "RandomPlayerbotMgr.h"

#include <cmath>
#include <list>
#include <mutex>
#include <unordered_map>

using namespace CoaCleanWorld;

namespace
{
constexpr float HOSTILE_SEARCH = 50.0f;
constexpr uint64 REVIVE_GIVE_UP_MS = 40000;  // the safe spot not reached in time: the spirit healer

std::mutex cleanLock;
std::unordered_map<uint64, uint64> reviveSince;  // ghost -> first try to reach a safe spot (map threads)
std::unordered_map<uint64, uint64> swimSince;    // bot -> idle in the water since (ms)

bool Enabled() { return sPlayerbotAIConfig.coaLiveCleanWorldEnable; }

bool FreeRandom(Player* bot, PlayerbotAI* botAI)
{
    return bot && botAI && sRandomPlayerbotMgr.IsRandomBot(bot) && !botAI->HasGameClientMaster() && !bot->InBattleground();
}

// The monsters around the bot that would attack it (alive, hostile, not critters).
std::vector<Point> Hostiles(Player* bot)
{
    std::list<Unit*> units;
    Acore::AnyUnfriendlyUnitInObjectRangeCheck check(bot, bot, HOSTILE_SEARCH);
    Acore::UnitListSearcher<Acore::AnyUnfriendlyUnitInObjectRangeCheck> searcher(bot, units, check);
    Cell::VisitObjects(bot, searcher, HOSTILE_SEARCH);
    std::vector<Point> out;
    for (Unit* u : units)
        if (Creature* c = u->ToCreature(); c && c->IsAlive() && !c->IsCritter() && !c->IsCivilian() && c->IsHostileTo(bot))
            out.push_back({c->GetPositionX(), c->GetPositionY()});
    return out;
}
}  // namespace

namespace CoaCleanWorld
{
bool PrepareRevive(PlayerbotAI* botAI)
{
    Player* bot = botAI ? botAI->GetBot() : nullptr;
    Corpse* corpse = bot ? bot->GetCorpse() : nullptr;
    if (!Enabled() || !corpse || !FreeRandom(bot, botAI))
        return false;

    uint64 const key = bot->GetGUID().GetRawValue();
    uint64 const now = GameTime::GetGameTimeMS().count();
    std::vector<Point> const hostiles = Hostiles(bot);
    if (!Unsafe({bot->GetPositionX(), bot->GetPositionY()}, hostiles))
    {
        std::lock_guard<std::mutex> guard(cleanLock);
        reviveSince.erase(key);
        return false;  // nothing on this spot: revive here
    }

    uint64 since = 0;
    {
        std::lock_guard<std::mutex> guard(cleanLock);
        since = reviveSince.try_emplace(key, now).first->second;
    }
    std::vector<Point> const spots = Around({corpse->GetPositionX(), corpse->GetPositionY()});
    int const pick = PickSafeSpot(spots, hostiles);
    float z = 0.0f;
    if (pick >= 0)
        z = bot->GetMap()->GetHeight(spots[pick].x, spots[pick].y, corpse->GetPositionZ() + 10.0f);
    if (pick < 0 || z <= INVALID_HEIGHT || now > since + REVIVE_GIVE_UP_MS)
    {
        {
            std::lock_guard<std::mutex> guard(cleanLock);
            reviveSince.erase(key);
        }
        return botAI->DoSpecificAction("spirit healer");
    }
    if (!bot->isMoving())
        bot->GetMotionMaster()->MovePoint(0, spots[pick].x, spots[pick].y, z);
    return true;
}

void LeaveWater(Player* bot, PlayerbotAI* botAI)
{
    if (!Enabled() || !FreeRandom(bot, botAI) || !bot->IsAlive())
        return;
    uint64 const key = bot->GetGUID().GetRawValue();
    uint64 const now = GameTime::GetGameTimeMS().count();
    NewRpgStatus const status = botAI->rpgInfo.GetStatus();
    bool const idle = status == RPG_IDLE || status == RPG_REST || status == RPG_WANDER_RANDOM;
    bool const inWater = bot->IsInWater();

    uint32 swimmingS = 0;
    {
        std::lock_guard<std::mutex> guard(cleanLock);
        if (!inWater || !idle || bot->IsInCombat())
        {
            swimSince.erase(key);
            return;
        }
        swimmingS = uint32((now - swimSince.try_emplace(key, now).first->second) / 1000);
    }
    if (!LeavesWater(inWater, idle, bot->IsInCombat(), swimmingS))
        return;

    // The nearest dry ground: eight directions, 15 to 60 yd.
    Map* map = bot->GetMap();
    for (float dist = 15.0f; dist <= 60.0f; dist += 15.0f)
        for (int i = 0; i < 8; ++i)
        {
            float const a = float(i) * 3.14159265f / 4.0f;
            float const x = bot->GetPositionX() + dist * std::cos(a);
            float const y = bot->GetPositionY() + dist * std::sin(a);
            float const z = map->GetHeight(x, y, bot->GetPositionZ() + 20.0f);
            if (z <= INVALID_HEIGHT || map->IsInWater(bot->GetPhaseMask(), x, y, z, bot->GetCollisionHeight()))
                continue;
            bot->GetMotionMaster()->MovePoint(0, x, y, z);
            std::lock_guard<std::mutex> guard(cleanLock);
            swimSince.erase(key);
            return;
        }
}
}  // namespace CoaCleanWorld
