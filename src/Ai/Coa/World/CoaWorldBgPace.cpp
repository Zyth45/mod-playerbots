/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

/*
 * CoA World: the pace of bot-only battlegrounds and arenas (AiPlayerbot.CoaWorld.BotOnlyBgPace.Enable).
 *
 * PerfMon at 3000 bots: about 47 bots in the two battlegrounds the bots keep going by themselves (no real player)
 * took 44% of the bots' AI time, about 50 times a bot outside (always active, an AI pass every 100 ms). With the
 * option, a bot in a battleground or arena where no real player is, and that no real player queues for, thinks
 * every IntervalMs (500) instead; it is back to normal as soon as a real player is on the map or in its queue
 * (seen within a second). Dungeons are left alone (bots level there).
 * The queues of the real players are read on the world thread every 2 s; the bots read the result in their map
 * thread.
 */

#include "CoaWorldBgPace.h"

#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "Config.h"
#include "Map.h"
#include "Player.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "WorldSession.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <set>

namespace
{
std::atomic<bool> g_on{ false };
uint32 g_intervalMs = 500;

std::mutex g_lock;
std::set<uint32> g_queued;  // BattlegroundQueueTypeId a real player is queued for
bool g_randomQueued = false;

bool RealPlayer(Player* player)
{
    return player && player->GetSession() && !player->GetSession()->IsBot() && !GET_PLAYERBOT_AI(player);
}

void ReadQueues()
{
    std::set<uint32> queued;
    bool random = false;
    for (Player* player : sRandomPlayerbotMgr.GetPlayers())
    {
        if (!RealPlayer(player) || !player->InBattlegroundQueue())
            continue;
        for (uint32 i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
        {
            BattlegroundQueueTypeId const queue = player->GetBattlegroundQueueTypeId(i);
            if (queue == BATTLEGROUND_QUEUE_NONE)
                continue;
            queued.insert(uint32(queue));
            if (queue == BATTLEGROUND_QUEUE_RB)
                random = true;
        }
    }
    std::lock_guard<std::mutex> lock(g_lock);
    g_queued.swap(queued);
    g_randomQueued = random;
}

class CoaWorldBgPaceScript : public WorldScript
{
public:
    CoaWorldBgPaceScript()
        : WorldScript("CoaWorldBgPaceScript", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE })
    {
    }

    void OnAfterConfigLoad(bool /*reload*/) override { Read(); }
    void OnStartup() override { Read(); }

    void OnUpdate(uint32 diff) override
    {
        if (!g_on)
            return;
        _timer += diff;
        if (_timer < 2 * IN_MILLISECONDS)
            return;
        _timer = 0;
        ReadQueues();
    }

private:
    static void Read()
    {
        g_on = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaWorld.BotOnlyBgPace.Enable", false, false);
        g_intervalMs = std::clamp<uint32>(sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaWorld.BotOnlyBgPace.IntervalMs", 500, false),
                                          100, 5000);
    }

    uint32 _timer = 2 * IN_MILLISECONDS;
};
}  // namespace

namespace CoaWorldBgPace
{
bool On() { return g_on; }

uint32 IntervalMs() { return g_intervalMs; }

bool BotOnly(Player* bot)
{
    if (!g_on || !bot || !bot->InBattleground())
        return false;
    Battleground* bg = bot->GetBattleground();
    Map* map = bot->GetMap();
    if (!bg || !map || !map->IsBattlegroundOrArena())
        return false;

    // A real player on the map (the bot's own map thread).
    for (auto const& ref : map->GetPlayers())
        if (RealPlayer(ref.GetSource()))
            return false;

    // A real player queued for this battleground or arena (or for a random battleground).
    uint32 const queue = uint32(BattlegroundMgr::BGQueueTypeId(bg->GetBgTypeID(), bg->GetArenaType()));
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_queued.count(queue) || (g_randomQueued && bg->isBattleground()))
        return false;
    return true;
}
}  // namespace CoaWorldBgPace

void AddSC_coa_world_bg_pace() { new CoaWorldBgPaceScript(); }
