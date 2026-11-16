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
 *
 * 3.1.4 (Guilds.PlayerGuildProfessions): the bot members brought online first are those who bring a profession the
 * guild has nobody online with (gathering, crafting, cooking, first aid: what guild orders need), and a bot that is the
 * only one online with one of them stays when its time is up. Which bots, never how many: the bot count is unchanged.
 * Guilds.PlayerGuildTeach: a profession no member has at all is learned by an online bot member with a free slot
 * (one a guild a minute, at its level's skill, the trainer's recipes with AutoLearnTradeskillSpells), so that
 * every guild of a real player has its miner, herbalist, skinner, crafters, cook and medic.
 */

#include "CoaWorldGuildPresence.h"
#include "CoaWorldBots.h"
#include "CoaWorldGuildRules.h"

#include "Config.h"
#include "Log.h"
#include "PlayerbotFactory.h"
#include "World.h"
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
bool g_professions = true;
bool g_teach = true;
std::unordered_map<uint32, uint32> g_taught;  // guild -> profession bits taught (the database sees them at the next save)

struct GuildBots
{
    std::vector<std::pair<uint32, uint32>> members;  // bot member (low guid, account)
    std::vector<uint32> professions;                 // the members' professions (CoaWorldGuildRules::ProfessionBit)
    uint32 online = 0;
    uint32 target = 0;
};
std::unordered_map<uint32, GuildBots> g_guilds;  // guilds with a real member online (world thread)
std::unordered_set<uint32> g_soleHolders;         // online bots alone online in their guild with a profession

bool Online(uint32 low) { return ObjectAccessor::FindConnectedPlayer(ObjectGuid::Create<HighGuid::Player>(low)) != nullptr; }

bool IsHuman(Player* player)
{
    return player && player->GetSession() && !player->GetSession()->IsBot() && !GET_PLAYERBOT_AI(player) && !CoaWorld::IsTestDriver(player);
}

// A profession no member of the guild has: an online bot member with a free slot learns it, as a trainer would
// teach it (starter spell, its level's skill, the recipes InitAvailableSpells teaches a bot of that profession).
void Teach(uint32 guildId, GuildBots const& g)
{
    uint32 membersMask = 0;
    for (uint32 mask : g.professions)
        membersMask |= mask;
    uint32& taught = g_taught[guildId];
    uint32 const bit = CoaWorldGuildRules::ProfessionToTeach(membersMask, taught);
    uint32 const skill = CoaWorldGuildRules::ProfessionSkill(bit);
    uint32 const starter = CoaWorldGuildRules::ProfessionStarterSpell(skill);
    if (!bit || !starter)
        return;
    uint32 const maxPrimary = std::min<uint32>(2, sWorld->getIntConfig(CONFIG_MAX_PRIMARY_TRADE_SKILL));
    for (auto const& [low, account] : g.members)
    {
        Player* bot = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(low));
        if (!bot || !bot->IsInWorld() || !GET_PLAYERBOT_AI(bot) || bot->IsInCombat() || bot->GetLevel() < 5)
            continue;
        std::vector<uint32> primaries;
        for (uint32 b = 1; b < (1u << 11); b <<= 1)
            if (uint32 const other = CoaWorldGuildRules::ProfessionSkill(b);
                CoaWorldGuildRules::IsPrimaryProfession(other) && bot->HasSkill(other))
                primaries.push_back(other);
        if (!CoaWorldGuildRules::MayLearnProfession(skill, uint32(primaries.size()), maxPrimary, bot->HasSkill(skill)))
            continue;

        uint32 const value = std::max<uint32>(1, std::min<uint32>(bot->GetLevel() * 5, 450));
        bot->learnSpell(starter, false);
        bot->SetSkill(uint16(skill), 1, uint16(value), uint16(value));
        if (CoaWorldGuildRules::IsPrimaryProfession(skill))
        {
            // The pair a later refresh of the bot keeps (PlayerbotFactory::InitTradeSkills), and the slot it takes.
            sRandomPlayerbotMgr.SetValue(bot, "firstSkill", primaries.empty() ? skill : primaries.front());
            sRandomPlayerbotMgr.SetValue(bot, "secondSkill", primaries.empty() ? 0 : skill);
            bot->SetFreePrimaryProfessions(uint16(maxPrimary > primaries.size() + 1 ? maxPrimary - primaries.size() - 1 : 0));
        }
        PlayerbotFactory(bot, bot->GetLevel()).InitAvailableSpells();
        taught |= bit;
        LOG_INFO("playerbots.coa", "CoA World: guild {}: {} learned profession {} at {} (nobody in the guild had it)", guildId,
                 bot->GetName(), skill, value);
        return;
    }
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
                g.professions.push_back(0);
                if (Online(low))
                    ++g.online;
            } while (result->NextRow());
        }
        if (g_professions && !fresh.empty())
            if (QueryResult result = CharacterDatabase.Query(
                    "SELECT gm.`guildid`, gm.`guid`, cs.`skill` FROM `guild_member` gm JOIN `character_skills` cs ON cs.`guid` = "
                    "gm.`guid` WHERE gm.`guildid` IN ({}) AND cs.`skill` IN (186,182,393,164,171,197,165,333,202,185,129) AND "
                    "cs.`value` > 0", in))
            {
                do
                {
                    Field* f = result->Fetch();
                    auto const g = fresh.find(f[0].Get<uint32>());
                    if (g == fresh.end())
                        continue;
                    uint32 const low = f[1].Get<uint32>();
                    for (std::size_t i = 0; i < g->second.members.size(); ++i)
                        if (g->second.members[i].first == low)
                            g->second.professions[i] |= CoaWorldGuildRules::ProfessionBit(f[2].Get<uint32>());
                } while (result->NextRow());
            }
    }
    std::unordered_set<uint32> sole;
    for (auto& [id, g] : fresh)
    {
        g.target = (uint32(g.members.size()) * g_pct + 99) / 100;
        if (!g_professions)
            continue;
        for (std::size_t i = 0; i < g.members.size(); ++i)
        {
            if (!g.professions[i] || !Online(g.members[i].first))
                continue;
            uint32 others = 0;
            for (std::size_t j = 0; j < g.members.size(); ++j)
                if (j != i && Online(g.members[j].first))
                    others |= g.professions[j];
            if (CoaWorldGuildRules::SoleProfessionHolder(g.professions[i], others))
                sole.insert(g.members[i].first);
        }
    }
    g_guilds.swap(fresh);
    g_soleHolders.swap(sole);
    if (g_professions && g_teach)
        for (auto const& [id, g] : g_guilds)
            Teach(id, g);
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
        g_professions = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaWorld.Guilds.PlayerGuildProfessions", true, false);
        g_teach = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaWorld.Guilds.PlayerGuildTeach", true, false);
        if (!g_on)
        {
            g_guilds.clear();
            g_soleHolders.clear();
        }
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
        // The professions the guild has nobody online with, first (within the bot count: which bots, not how many).
        if (g_professions)
        {
            uint32 onlineMask = 0;
            std::vector<uint32> offline;
            std::vector<std::size_t> offlineIndex;
            for (std::size_t i = 0; i < g.members.size(); ++i)
                if (Online(g.members[i].first))
                    onlineMask |= g.professions[i];
                else
                {
                    offline.push_back(g.professions[i]);
                    offlineIndex.push_back(i);
                }
            for (std::size_t pick : CoaWorldGuildRules::ProfessionCover(onlineMask, offline))
                first.insert(g.members[offlineIndex[pick]].second);
        }
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
    if (itr == g_guilds.end())
        return false;
    return itr->second.online <= itr->second.target || g_soleHolders.count(bot->GetGUID().GetCounter()) != 0;
}

void AddSC_coa_world_guild_presence() { new CoaWorldGuildPresenceScript(); }
