/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

/*
 * CoA World: the SquidBots Lite world protocol, prefix SQBW (D:\CoA-Addon\docs\WORLD-PROTOCOLE-3.0.md §0, §1, §4).
 *
 * The addon whispers its own player "SQBW\t<request>"; the server answers with addon whispers to that player, from
 * that player, one line each, the last one "-". This file answers GORD and GLIST (guild orders, CoaWorldGuilds) and
 * WEV? (invasions, rushes, server firsts), and pushes the WE lines of an event that is announced, starts or ends to
 * the players who asked WEV? less than 30 minutes ago. Other SQBW words (artisans, soul) go on to their owners.
 *
 * Everything is checked here: the sender is the only target, quantities by the brick's own options. One request per
 * second per player (more: dropped), list answers kept 10 s per player. Off (AiPlayerbot.CoaWorld.Guilds.Addon,
 * .Events.Addon at 0, or their bricks off): no answer at all. The chat hook runs in the world thread.
 */

#include "CoaWorldAddonRules.h"
#include "CoaWorldCapitalRaid.h"
#include "CoaWorldGuildActivities.h"
#include "CoaWorldGuilds.h"
#include "CoaWorldState.h"

#include "Chat.h"
#include "Config.h"
#include "GameTime.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <map>
#include <unordered_map>

using namespace CoaWorldAddonRules;

namespace
{
bool g_eventsAddon = false;

struct Asker
{
    uint64 lastRequestMs = 0;
    int64 lastWev = 0;
    std::map<std::string, std::pair<uint64, std::vector<std::string>>> cache;  // request -> (built at, lines)
};
std::unordered_map<uint32, Asker> g_askers;  // world thread only

void Send(Player* player, std::string const& line)
{
    WorldPacket data;
    ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, LANG_ADDON, player, player, std::string(PREFIX) + "\t" + line);
    player->SendDirectMessage(&data);
}

void Answer(Player* player, std::vector<std::string> const& lines)
{
    for (std::string const& line : lines)
        Send(player, line);
    Send(player, "-");
}

std::vector<std::string> WorldLife(Player* player)
{
    std::vector<std::string> out;
    if (CoaWorldAddon::InvasionsOn())
        CoaWorldAddon::Invasions(player, out);
    if (CoaWorldAddon::RushOn())
        CoaWorldAddon::Rushes(player, out);
    if (CoaWorldAddon::FirstsOn())
        CoaWorldAddon::Firsts(out);
    if (CoaWorldCapitalRaidOn())
        CoaWorldCapitalRaidLines(out);
    // A game master (what .coaworld invasion/rush start and stop need): the addon shows its GM buttons. The commands'
    // own checks stay the guard.
    if (player->GetSession() && player->GetSession()->GetSecurity() >= SEC_GAMEMASTER)
        out.push_back("WGM\t1");
    return out;
}

class CoaWorldAddonPlayerScript : public PlayerScript
{
public:
    CoaWorldAddonPlayerScript()
        : PlayerScript("CoaWorldAddonPlayerScript", { PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT, PLAYERHOOK_ON_LOGOUT })
    {
    }

    void OnPlayerLogout(Player* player) override
    {
        if (player)
            g_askers.erase(player->GetGUID().GetCounter());
    }

    // Returns false only for this session's own words: the hook chain stops at the first false.
    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 lang, std::string& msg, Player* receiver) override
    {
        if (type != CHAT_MSG_WHISPER || lang != LANG_ADDON || !player || receiver != player)
            return true;
        std::vector<std::string> const request = Request(msg);
        if (request.empty() || !Mine(request[0]))
            return true;

        std::string const& word = request[0];
        bool const gifts = word == "GOFF" || word == "GACC" || word == "GREF";
        bool const on = word == "WEV?" ? CoaWorldAddon::On()
                        : gifts            ? CoaWorldGuildGiftsOn()
                        : word == "GCAT"   ? CoaWorldGuildCatalogOn()
                        : word == "GACT?" || word == "GACT" ? CoaWorldGuildActivitiesOn()
                                           : CoaWorldGuildAddonOn();
        if (!on || !player->GetSession() || player->GetSession()->IsBot())
            return false;

        uint64 const nowMs = GameTime::GetGameTimeMS().count();
        Asker& asker = g_askers[player->GetGUID().GetCounter()];
        if (!RateOk(asker.lastRequestMs, nowMs))
            return false;
        asker.lastRequestMs = nowMs;

        if (word == "GACC" || word == "GREF")
        {
            Answer(player, CoaWorldGuildGiftAnswer(player, request, word == "GACC"));
            return false;
        }
        if (word == "GACT?")
        {
            Answer(player, CoaWorldGuildActivityList(player));
            return false;
        }
        if (word == "GACT")
        {
            Answer(player, CoaWorldGuildActivityStart(player, request));
            return false;
        }
        if (word == "GCAT")
        {
            // Its own cache (5 min per guild, CoaWorldGuilds); the search and page change at each request.
            Answer(player, CoaWorldGuildCatalog(player, request));
            return false;
        }
        if (word == "GOFF")
        {
            Answer(player, CoaWorldGuildGiftList(player));  // never cached: an offer comes and goes
            return false;
        }
        if (word == "GORD")
        {
            Answer(player, CoaWorldGuildAddonOrder(player, request));
            asker.cache.erase("GLIST");  // the new order shows at once
            return false;
        }

        if (word == "WEV?")
            asker.lastWev = time(nullptr);
        auto& cached = asker.cache[word];
        if (!CacheFresh(cached.first, nowMs))
            cached = { nowMs, word == "WEV?" ? WorldLife(player) : CoaWorldGuildAddonList(player) };
        Answer(player, cached.second);
        return false;
    }
};

class CoaWorldAddonWorldScript : public WorldScript
{
public:
    CoaWorldAddonWorldScript()
        : WorldScript("CoaWorldAddonWorldScript", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP })
    {
    }

    void OnAfterConfigLoad(bool /*reload*/) override { Read(); }
    void OnStartup() override { Read(); }

private:
    static void Read() { g_eventsAddon = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaWorld.Events.Addon", true, false); }
};
}  // namespace

void CoaWorldAddonSendLines(Player* player, std::vector<std::string> const& lines)
{
    if (player && player->IsInWorld())
        Answer(player, lines);
}

bool CoaWorldAddon::On()
{
    return g_eventsAddon && (CoaWorldAddon::InvasionsOn() || CoaWorldAddon::RushOn() || CoaWorldAddon::FirstsOn());
}

void CoaWorldAddon::Push(std::function<std::string(Player*)> const& lineFor)
{
    if (!On())
        return;
    int64 const now = time(nullptr);
    for (auto& [guid, asker] : g_askers)
    {
        asker.cache.erase("WEV?");  // an event changed: the next WEV? is built anew
        if (!PushWanted(asker.lastWev, now))
            continue;
        Player* player = ObjectAccessor::FindPlayerByLowGUID(guid);
        if (!player || !player->IsInWorld())
            continue;
        std::string const line = lineFor(player);
        if (!line.empty())
            Answer(player, { line });
    }
}

void AddSC_coa_world_addon()
{
    new CoaWorldAddonPlayerScript();
    new CoaWorldAddonWorldScript();
}
