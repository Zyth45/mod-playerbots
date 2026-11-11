/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

/*
 * CoA World: a real player's guild is not empty (AiPlayerbot.CoaWorld.Guilds.PlayerGuildOnline, under Guilds.Enable).
 *
 * With the online/offline rotation (EnablePeriodicOnlineOffline), a guild of 60 bots looked empty to its player.
 * While a real member of a guild is online, the rotation logs in first the bot members of that guild, up to
 * PlayerGuildOnlinePercent (40) of them, within the bot count; and it keeps them in the world (they log out last)
 * while the guild has no more than that share online. Checked every minute on the world thread.
 */

#include "CoaWorldGuildPresence.h"

#include "Config.h"
#include "DatabaseEnv.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "WorldSession.h"

#include <algorithm>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
bool g_on = false;
uint32 g_pct = 40;

struct GuildBots
{
    std::vector<std::pair<uint32, uint32>> members;  // bot member (low guid, account)
    uint32 online = 0;
    uint32 target = 0;
};
std::unordered_map<uint32, GuildBots> g_guilds;  // guilds with a real member online (world thread)

bool IsHuman(Player* player)
{
    return player && player->GetSession() && !player->GetSession()->IsBot() && !GET_PLAYERBOT_AI(player);
}

void Refresh()
{
    std::set<uint32> guilds;
    for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
        if (IsHuman(player) && player->IsInWorld() && player->GetGuildId())
            guilds.insert(player->GetGuildId());
    std::unordered_map<uint32, GuildBots> fresh;
    if (!guilds.empty())
    {
        std::string in;
        for (uint32 id : guilds)
            in += (in.empty() ? "" : ",") + std::to_string(id);
        if (QueryResult result = CharacterDatabase.Query(
                "SELECT gm.`guildid`, gm.`guid`, c.`account` FROM `guild_member` gm JOIN `characters` c ON c.`guid` = gm.`guid` "
                "WHERE gm.`guildid` IN ({})", in))
        {
            do
            {
                Field* f = result->Fetch();
                uint32 const low = f[1].Get<uint32>();
                if (!sRandomPlayerbotMgr.IsRandomBot(low))
                    continue;
                GuildBots& g = fresh[f[0].Get<uint32>()];
                g.members.emplace_back(low, f[2].Get<uint32>());
                if (ObjectAccessor::FindConnectedPlayer(ObjectGuid::Create<HighGuid::Player>(low)))
                    ++g.online;
            } while (result->NextRow());
        }
    }
    for (auto& [id, g] : fresh)
        g.target = (uint32(g.members.size()) * g_pct + 99) / 100;
    g_guilds.swap(fresh);
}

class CoaWorldGuildPresenceScript : public WorldScript
{
public:
    CoaWorldGuildPresenceScript()
        : WorldScript("CoaWorldGuildPresenceScript", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE })
    {
    }

    void OnAfterConfigLoad(bool /*reload*/) override { Read(); }
    void OnStartup() override { Read(); }

    void OnUpdate(uint32 diff) override
    {
        if (!g_on)
            return;
        _timer += diff;
        if (_timer < MINUTE * IN_MILLISECONDS)
            return;
        _timer = 0;
        Refresh();
    }

private:
    static void Read()
    {
        g_on = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaWorld.Guilds.Enable", true, false) &&
               sConfigMgr->GetOption<bool>("AiPlayerbot.CoaWorld.Guilds.PlayerGuildOnline", true, false);
        g_pct = std::min<uint32>(100, sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaWorld.Guilds.PlayerGuildOnlinePercent", 40, false));
        if (!g_on)
            g_guilds.clear();
    }

    uint32 _timer = MINUTE * IN_MILLISECONDS;
};
}  // namespace

void CoaWorldGuildPresenceOrderAccounts(std::vector<uint32>& accounts)
{
    if (!g_on || g_guilds.empty() || accounts.empty())
        return;
    std::unordered_set<uint32> first;
    for (auto const& [id, g] : g_guilds)
    {
        if (g.online >= g.target)
            continue;
        uint32 need = g.target - g.online;
        for (auto const& [low, account] : g.members)
        {
            if (!need)
                break;
            if (ObjectAccessor::FindConnectedPlayer(ObjectGuid::Create<HighGuid::Player>(low)))
                continue;
            if (first.insert(account).second)
                --need;
        }
    }
    if (!first.empty())
        std::stable_partition(accounts.begin(), accounts.end(), [&first](uint32 account) { return first.count(account) != 0; });
}

bool CoaWorldGuildPresenceKeep(Player* bot)
{
    if (!g_on || !bot || !bot->GetGuildId())
        return false;
    auto const itr = g_guilds.find(bot->GetGuildId());
    return itr != g_guilds.end() && itr->second.online <= itr->second.target;
}

void AddSC_coa_world_guild_presence() { new CoaWorldGuildPresenceScript(); }
