/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

/*
 * CoA World: a time budget for the bots' AI per map tick (AiPlayerbot.CoaWorld.AiBudgetMs, 0 = off).
 *
 * A peak study (3000 bots): 96% of the map update peaks are the sum of 1400-1500 bot updates on one
 * continent's thread, no bot above 10-15 ms. With a budget, once the bots' AI of a map has taken AiBudgetMs in a tick,
 * the next bots skip their AI for that tick and keep the elapsed time for their next pass. A bot that skipped runs
 * first-class on the next tick (even over the budget): none waits two ticks in a row. Never skipped: a bot in combat,
 * grouped with a real player, or within 100 yards of one. Player::Update itself is not touched; only the AI's time is
 * counted. The tick is (map, game time in ms), the same through one world update; the state is per map thread.
 * Skips are counted in PerfMon ("CoaWorld::AiBudgetSkip").
 */

#include "CoaWorldAiBudget.h"

#include "Config.h"
#include "GameTime.h"
#include "Group.h"
#include "Map.h"
#include "PerfMonitor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "ScriptMgr.h"
#include "WorldSession.h"

#include <algorithm>

namespace
{
uint64 g_budgetUs = 0;

struct Tick
{
    Map const* map = nullptr;
    uint64 at = 0;
    uint64 spentUs = 0;
};
thread_local Tick t_tick;

Tick& Current(Player* bot)
{
    Map const* map = bot->GetMap();
    uint64 const at = GameTime::GetGameTimeMS().count();
    if (t_tick.map != map || t_tick.at != at)
        t_tick = { map, at, 0 };
    return t_tick;
}

bool RealGroup(Player* bot)
{
    Group* group = bot->GetGroup();
    if (!group)
        return false;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        if (Player* member = ref->GetSource())
            if (member != bot && member->GetSession() && !member->GetSession()->IsBot() && !GET_PLAYERBOT_AI(member))
                return true;
    return false;
}

class CoaWorldAiBudgetScript : public WorldScript
{
public:
    CoaWorldAiBudgetScript() : WorldScript("CoaWorldAiBudgetScript", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP }) { }
    void OnAfterConfigLoad(bool /*reload*/) override { Read(); }
    void OnStartup() override { Read(); }

private:
    static void Read()
    {
        g_budgetUs = uint64(std::min<uint32>(1000, sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaWorld.AiBudgetMs", 0, false))) * 1000;
    }
};
}  // namespace

namespace CoaWorldAiBudget
{
bool On() { return g_budgetUs != 0; }

bool Skip(Player* bot, PlayerbotAI* botAI)
{
    if (!g_budgetUs || !bot || !botAI || !bot->GetMap())
        return false;
    Tick& tick = Current(bot);
    bool const over = tick.spentUs >= g_budgetUs;
    if (!over)
    {
        botAI->coaBudgetSkipped = false;
        return false;
    }
    // Over the budget: the exemptions, checked only now.
    bool const skip = CoaWorldAiBudgetRules::Skip(true, botAI->coaBudgetSkipped, bot->IsInCombat(), RealGroup(bot),
                                                  botAI->HasPlayerNearby(100.0f));
    botAI->coaBudgetSkipped = skip;
    if (skip)
        if (PerfMonitorOperation* pmo = sPerfMonitor.start(PERF_MON_TOTAL, "CoaWorld::AiBudgetSkip"))
            pmo->finish();
    return skip;
}

void Spent(Player* bot, uint64 us)
{
    if (!g_budgetUs || !bot || !bot->GetMap())
        return;
    Current(bot).spentUs += us;
}
}  // namespace CoaWorldAiBudget

void AddSC_coa_world_ai_budget() { new CoaWorldAiBudgetScript(); }
