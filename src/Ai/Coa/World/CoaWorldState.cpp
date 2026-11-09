/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA World: see CoaWorldState.h. Built on the world thread, written to monde.json.tmp then renamed.

#include "CoaWorldState.h"

#include "CoaWorldCapitalRaid.h"
#include "CoaWorldStateRules.h"
#include "Config.h"
#include "GameTime.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "Timer.h"
#include "WorldSession.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <vector>

using namespace CoaWorldStateRules;

namespace
{
constexpr uint32 WATCH_CHECK_MS = 5000;
constexpr uint32 EVERY_S = 30;
constexpr size_t KEEP = 20;

std::atomic<bool> g_on{ false };
std::filesystem::path g_dir;

std::mutex g_lock;
struct OrderRow
{
    uint32 id;
    std::string guild, by;
    uint32 item, count;
    std::string state;
    time_t at;
};
std::deque<OrderRow> g_orders;
uint32 g_nextOrder = 1;
struct FirstRow
{
    std::string what, by, guild;
    time_t at;
};
std::deque<FirstRow> g_firsts;
std::deque<std::pair<int64, uint32>> g_stuck, g_unstuck;  // (when, bot)
int64 g_started = 0;

bool WatchAsks()
{
    std::error_code error;
    std::filesystem::path const watch = g_dir / "watch.json";
    if (!std::filesystem::exists(watch, error))
        return false;
    std::ifstream in(watch, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    auto const fileTime = std::filesystem::last_write_time(watch, error);
    int64 const fileEpoch = error ? 0
                                  : std::chrono::duration_cast<std::chrono::seconds>(
                                        (fileTime - std::filesystem::file_time_type::clock::now()) +
                                        std::chrono::system_clock::now().time_since_epoch())
                                        .count();
    return Watched(text.str().substr(0, 4096), "monde", fileEpoch, time(nullptr));
}

// The guilds with someone online: members, online, bots online, bank.
std::string GuildsJson()
{
    struct Count
    {
        uint32 online = 0, bots = 0;
    };
    std::map<uint32, Count> counts;
    {
        std::shared_lock<std::shared_mutex> lock(*HashMapHolder<Player>::GetLock());
        for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
        {
            if (!player || !player->IsInWorld() || !player->GetGuildId())
                continue;
            Count& c = counts[player->GetGuildId()];
            ++c.online;
            if (player->GetSession() && player->GetSession()->IsBot())
                ++c.bots;
        }
    }
    std::vector<std::pair<uint32, Count>> sorted(counts.begin(), counts.end());
    std::sort(sorted.begin(), sorted.end(), [](auto const& a, auto const& b) { return a.second.online > b.second.online; });
    if (sorted.size() > 50)
        sorted.resize(50);

    std::string out = "[";
    bool first = true;
    for (auto const& [id, c] : sorted)
    {
        Guild* guild = sGuildMgr->GetGuildById(id);
        if (!guild)
            continue;
        out += std::string(first ? "" : ",") + "{\"id\":" + std::to_string(id) + ",\"name\":" + Quote(guild->GetName()) +
               ",\"members\":" + std::to_string(guild->GetMemberCount()) + ",\"bots\":" + std::to_string(c.bots) +
               ",\"online\":" + std::to_string(c.online) + ",\"bank_gold\":" + std::to_string(guild->GetTotalBankMoney()) +
               ",\"orders_open\":null}";
        first = false;
    }
    return out + "]";
}

std::string Build(time_t now)
{
    std::string orders = "[", firsts = "[";
    uint32 stuck, unstuck, stuckEvents = 0, unstuckEvents = 0;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        uint32 n = 0;
        for (OrderRow const& o : g_orders)
            orders += std::string(n++ ? "," : "") + "{\"id\":" + std::to_string(o.id) + ",\"guild\":" + Quote(o.guild) + ",\"item\":" + std::to_string(o.item) +
                      ",\"count\":" + std::to_string(o.count) + ",\"state\":" + Quote(o.state) + ",\"by\":" + Quote(o.by) +
                      ",\"at\":" + std::to_string(int64(o.at)) + "}";
        n = 0;
        for (auto f = g_firsts.rbegin(); f != g_firsts.rend(); ++f)
            firsts += std::string(n++ ? "," : "") + "{\"what\":" + Quote(f->what) + ",\"by\":" + Quote(f->by) +
                      ",\"guild\":" + Quote(f->guild) + ",\"at\":" + DateOrNull(f->at) + "}";
        // Bots, not detections; over 24 h, or since the start when the server runs for less ("since").
        auto distinct = [now](std::deque<std::pair<int64, uint32>>& list, uint32& events)
        {
            while (!list.empty() && now - list.front().first >= 86400)
                list.pop_front();
            events = uint32(list.size());
            std::set<uint32> bots;
            for (auto const& [when, bot] : list)
                bots.insert(bot);
            return uint32(bots.size());
        };
        stuck = distinct(g_stuck, stuckEvents);
        unstuck = distinct(g_unstuck, unstuckEvents);
    }
    orders += "]";
    firsts += "]";
    return "{\"invasions\":" + CoaWorld::InvasionsJson(now) + ",\"rushes\":" + CoaWorld::RushesJson(now) +
           ",\"guilds\":" + GuildsJson() + ",\"guild_orders\":" + orders + ",\"server_firsts\":" + firsts +
           ",\"recovery_24h\":{\"stuck\":" + std::to_string(stuck) + ",\"unstuck\":" + std::to_string(unstuck) +
           ",\"stuck_events\":" + std::to_string(stuckEvents) + ",\"unstuck_events\":" + std::to_string(unstuckEvents) +
           ",\"since\":" + std::to_string(std::max<int64>(g_started, int64(now) - 86400)) +
           "},\"dungeon_sim\":" + CoaWorld::DungeonSimJson(now) + ",\"bg_sim\":" + CoaWorld::BgSimJson(now) +
           ",\"capital_raid\":" + CoaWorldCapitalRaidJson() + "}";
}

void Write(std::string const& text)
{
    std::error_code error;
    std::filesystem::create_directories(g_dir, error);
    std::filesystem::path const tmp = g_dir / "monde.json.tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return;
        out << text;
    }
    std::filesystem::rename(tmp, g_dir / "monde.json", error);
}

class CoaWorldStateScript : public WorldScript
{
public:
    CoaWorldStateScript() : WorldScript("CoaWorldStateScript", { WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE }) { }

    void OnStartup() override
    {
        g_started = int64(time(nullptr));
        g_on = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaWorld.StateFile.Enable", false, false) &&
               sConfigMgr->GetOption<bool>("AiPlayerbot.CoaDataFiles", true, false);
        std::string dir = sConfigMgr->GetOption<std::string>("AiPlayerbot.CoaDataDir", "", false);
        g_dir = dir.empty() ? std::filesystem::path(sConfigMgr->GetOption<std::string>("LogsDir", "", false)) / "squidbot"
                            : std::filesystem::path(dir);
    }

    void OnUpdate(uint32 diff) override
    {
        if (!g_on)
            return;
        _watchTimer += diff;
        if (_watchTimer >= WATCH_CHECK_MS)
        {
            _watchTimer = 0;
            _watched = WatchAsks();
        }
        if (!_watched)
            return;
        _buildTimer += diff;
        if (_buildTimer < EVERY_S * IN_MILLISECONDS)
            return;
        _buildTimer = 0;

        time_t const now = time(nullptr);
        auto const start = std::chrono::steady_clock::now();
        std::string const data = Build(now);
        int64 const buildUs =
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
        Write("{\"schema\":\"squidbot.monde/1\",\"at\":" + std::to_string(int64(now)) + ",\"every\":" +
              std::to_string(EVERY_S) + ",\"build_us\":" + std::to_string(buildUs) + ",\"server_start\":" +
              std::to_string(int64(GameTime::GetStartTime().count())) + ",\"module\":null,\"data\":" + data + "}");
    }

private:
    uint32 _watchTimer = WATCH_CHECK_MS;
    uint32 _buildTimer = EVERY_S * IN_MILLISECONDS;  // the first one as soon as watched
    bool _watched = false;
};
}  // namespace

namespace CoaWorldState
{
void Order(std::string const& guild, uint32 item, uint32 count, char const* state, std::string const& by)
{
    if (!g_on)
        return;
    std::lock_guard<std::mutex> lock(g_lock);
    g_orders.push_back({ g_nextOrder++, guild, by, item, count, state, time(nullptr) });
    while (g_orders.size() > KEEP)
        g_orders.pop_front();
}

void Recovery(uint32 botLowGuid, bool unstuck)
{
    if (!g_on)
        return;
    std::lock_guard<std::mutex> lock(g_lock);
    int64 const now = int64(time(nullptr));
    auto& list = unstuck ? g_unstuck : g_stuck;
    list.emplace_back(now, botLowGuid);
    while (!list.empty() && now - list.front().first >= 86400)
        list.pop_front();
}

int64 TodaySince(time_t now)
{
    return std::max<int64>(int64(Acore::Time::GetLocalHourTimestamp(now, 0, false)), g_started);
}

void First(std::string const& what, std::string const& by, std::string const& guild, time_t at)
{
    if (!g_on)
        return;
    std::lock_guard<std::mutex> lock(g_lock);
    g_firsts.push_back({ what, by, guild, at });
    while (g_firsts.size() > KEEP)
        g_firsts.pop_front();
}
}  // namespace CoaWorldState

void AddSC_coa_world_state() { new CoaWorldStateScript(); }
