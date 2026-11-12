/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

/*
 * CoA World: the bots where the players are.
 *
 * Works with the online/offline rotation of the random bots (AiPlayerbot.EnablePeriodicOnlineOffline, a pool
 * larger than the bots online). Two things:
 *  - who logs in: the accounts with a character saved in the zone of a real player online go first
 *    (after OnlineSelection.FollowPlayerLevels, which keeps its order inside each group);
 *  - who logs out early: while more than FarSharePct of the random bots online are far from every real
 *    player (not in a real player's zone, nor within NearRange yards of one), a few of them a minute
 *    (RetirePerMinute) end their time in the world now; the rotation logs in others in their place, near the
 *    players. Offline, a bot costs nothing: the CPU goes where someone can see it.
 * A bot that is grouped, borrowed by a CoA World event, held, in a battleground or seen by a real player is
 * never sent off. With nobody online, nothing moves.
 *
 * The idea (logging bots in by proximity, from cmangos' LoginMgr) is written again here; no code taken.
 * World thread only. Config: AiPlayerbot.CoaWorld.Density.*, off by default.
 */

#include "CoaWorldBots.h"
#include "CoaWorldZoneLevels.h"

#include "Chat.h"
#include "ChatCommand.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include "WorldSession.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace Acore::ChatCommands;

namespace
{
    using namespace CoaWorld;

    constexpr char const* PREFIX = "AiPlayerbot.CoaWorld.Density.";

    struct Settings
    {
        bool enabled = false;
        uint32 farSharePct = 30;
        uint32 retirePerMinute = 10;
        float nearRange = 800.0f;
        bool rotation = false;  // AiPlayerbot.EnablePeriodicOnlineOffline
    };

    Settings settings;

    struct Counters
    {
        uint32 nearBots = 0;
        uint32 farBots = 0;
        uint32 retired = 0;
        uint32 orderedFirst = 0;
    };

    Counters counters;

    bool IsHumanPlayer(Player* player)
    {
        return player && player->IsInWorld() && !GET_PLAYERBOT_AI(player) && !player->IsGameMaster() &&
               player->GetSession() && !player->GetSession()->IsBot() && !CoaWorld::IsTestDriver(player);
    }

    std::vector<Player*> Humans()
    {
        std::vector<Player*> out;
        for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
            if (IsHumanPlayer(player))
                out.push_back(player);
        return out;
    }

    // Levels within this of a zone's range (or of the player's level) fit it.
    constexpr uint8 LEVEL_MARGIN = 5;

    // Near a player, and at a level for where it stands (seen in a test: level 55 bots walking in Elwynn were kept
    // there as "near" the player).
    bool IsNear(Player* bot, std::vector<Player*> const& humans)
    {
        for (Player* human : humans)
            if ((human->GetZoneId() == bot->GetZoneId() ||
                 (human->GetMapId() == bot->GetMapId() && human->GetExactDist(bot) < settings.nearRange)) &&
                CoaWorldZones::Fits(bot->GetZoneId(), bot->GetLevel(), human->GetLevel(), LEVEL_MARGIN))
                return true;
        return false;
    }

    void Retire()
    {
        std::vector<Player*> humans = Humans();
        if (humans.empty())
            return;
        std::vector<Player*> farList;
        uint32 nearCount = 0;
        for (auto const& [guid, bot] : RandomBots())
        {
            if (!bot || !bot->IsInWorld())
                continue;
            if (IsNear(bot, humans))
            {
                ++nearCount;
                continue;
            }
            farList.push_back(bot);
        }
        counters.nearBots = nearCount;
        counters.farBots = uint32(farList.size());

        uint32 total = nearCount + uint32(farList.size());
        uint32 keepFar = total * std::min<uint32>(settings.farSharePct, 100) / 100;
        if (farList.size() <= keepFar)
            return;

        std::shuffle(farList.begin(), farList.end(), std::mt19937(urand(0, 0x7FFFFFFF)));
        uint32 toRetire = std::min<uint32>(settings.retirePerMinute, uint32(farList.size()) - keepFar);
        for (Player* bot : farList)
        {
            if (!toRetire)
                break;
            PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
            if (!botAI || botAI->GetMaster() || bot->GetGroup() || bot->InBattleground() || bot->InArena() ||
                bot->InBattlegroundQueue() || bot->IsInCombat() || !IsFreeBot(bot) ||
                IsHeld(bot->GetGUID().GetCounter()) || botAI->HasPlayerNearby(150.0f))
                continue;
            sRandomPlayerbotMgr.CoaRetireBot(bot->GetGUID().GetCounter());
            ++counters.retired;
            --toRetire;
        }
    }

    void LoadSettings()
    {
        auto key = [](char const* name) { return std::string(PREFIX) + name; };
        settings.enabled = sConfigMgr->GetOption<bool>(key("Enable"), true, false);
        settings.farSharePct = std::min<uint32>(100, sConfigMgr->GetOption<uint32>(key("FarSharePct"), 30, false));
        settings.retirePerMinute = std::min<uint32>(200, sConfigMgr->GetOption<uint32>(key("RetirePerMinute"), 10, false));
        settings.nearRange = float(std::clamp<uint32>(sConfigMgr->GetOption<uint32>(key("NearRange"), 800, false), 100, 5000));
        settings.rotation = sConfigMgr->GetOption<bool>("AiPlayerbot.EnablePeriodicOnlineOffline", false, false);
        if (settings.enabled && !settings.rotation)
            LOG_WARN("module", "CoA World: AiPlayerbot.CoaWorld.Density needs AiPlayerbot.EnablePeriodicOnlineOffline = 1 "
                "(a pool of bots larger than those online); it does nothing without it.");
    }
}

// For RandomPlayerbotMgr::AddRandomBots: puts first the accounts with a character saved in the zone of a real
// player online, keeping the order inside each group. Off, or nobody online: a plain bool, nothing changes.
void CoaWorldDensityOrderAccounts(std::vector<uint32>& accounts)
{
    if (!settings.enabled || !settings.rotation || accounts.empty())
        return;

    // The players' zones, with the highest player level in each.
    std::unordered_map<uint32, uint8> zones;
    for (Player* h : Humans())
    {
        uint8& level = zones[h->GetZoneId()];
        level = std::max<uint8>(level, h->GetLevel());
    }
    if (zones.empty())
        return;

    // The same players' zones within a minute: the accounts found last time are still the ones near them.
    static std::unordered_map<uint32, uint8> cachedZones;
    static std::unordered_set<uint32> nearAccounts;
    static time_t cachedAt = 0;
    time_t now = time(nullptr);
    if (zones != cachedZones || now - cachedAt >= 60)
    {
        cachedZones = zones;
        cachedAt = now;
        nearAccounts.clear();
        auto const [minimum, maximum] = std::minmax_element(accounts.begin(), accounts.end());
        if (QueryResult result = CharacterDatabase.Query(
                "SELECT account, zone, level FROM characters WHERE account BETWEEN {} AND {}", *minimum, *maximum))
        {
            do
            {
                Field* fields = result->Fetch();
                uint32 const zone = fields[1].Get<uint32>();
                auto const itr = zones.find(zone);
                // Saved in a player's zone at a level for it: a level 60 saved in Elwynn does not come first.
                if (itr != zones.end() &&
                    CoaWorldZones::Fits(zone, fields[2].Get<uint8>(), itr->second, LEVEL_MARGIN))
                    nearAccounts.insert(fields[0].Get<uint32>());
            } while (result->NextRow());
        }
    }
    auto split = std::stable_partition(accounts.begin(), accounts.end(),
        [](uint32 account) { return nearAccounts.contains(account); });
    counters.orderedFirst = uint32(std::distance(accounts.begin(), split));
}

class CoaWorldDensityWorldScript : public WorldScript
{
public:
    CoaWorldDensityWorldScript()
        : WorldScript("CoaWorldDensityWorldScript", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE })
    {
    }

    void OnAfterConfigLoad(bool reload) override
    {
        if (reload)
            LoadSettings();
    }

    void OnStartup() override { LoadSettings(); }

    void OnUpdate(uint32 diff) override
    {
        if (!settings.enabled || !settings.rotation)
            return;
        _timer += diff;
        if (_timer < MINUTE * IN_MILLISECONDS)
            return;
        _timer = 0;
        Retire();
    }

private:
    uint32 _timer = 0;
};

class CoaWorldDensityCommandScript : public CommandScript
{
public:
    CoaWorldDensityCommandScript() : CommandScript("CoaWorldDensityCommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable densityTable = {
            { "status", HandleStatus, SEC_GAMEMASTER, Console::Yes },
        };
        static ChatCommandTable worldTable = {
            { "density", densityTable },
        };
        static ChatCommandTable commandTable = {
            { "coaworld", worldTable },
        };
        return commandTable;
    }

    static bool HandleStatus(ChatHandler* handler)
    {
        handler->PSendSysMessage("Bots near real players: {}, far: {} (kept far: {}%). Sent off early: {}. "
            "Last login batch: {} accounts near players first.{}", counters.nearBots, counters.farBots, settings.farSharePct,
            counters.retired, counters.orderedFirst,
            !settings.enabled ? " (switched off)" : !settings.rotation ? " (needs EnablePeriodicOnlineOffline = 1)" : "");
        return true;
    }
};

void AddSC_coa_world_density()
{
    new CoaWorldDensityWorldScript();
    new CoaWorldDensityCommandScript();
}
