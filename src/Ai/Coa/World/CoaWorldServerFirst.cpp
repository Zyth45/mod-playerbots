/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

/*
 * CoA World: "server first" announcements, for the "new living server" preset (PRESETS-3.0).
 *
 * For each side: the first character to reach the highest level, and the first kill of each raid boss,
 * told to the whole realm. Bots count as anyone else: on a new realm the bots levelling with the players
 * are part of the race. Each first is kept in the playerbots database (coa_world_server_first), so it is
 * told once in the life of the realm. Turned on over an older realm, a side that already has characters
 * at the top level gets no "first" for it (marked as taken at startup); raid kills count from then on.
 *
 * Kills and level-ups come from map threads; they are queued and handled by the world thread.
 * Config: AiPlayerbot.CoaWorld.ServerFirst.Enable, off by default.
 */

#include "CoaWorldBots.h"

#include "Chat.h"
#include "Config.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include "World.h"

#include <mutex>
#include <set>
#include <tuple>
#include <vector>

namespace
{
    enum FirstKind : uint8
    {
        FIRST_LEVEL = 1,  // entry: the level
        FIRST_BOSS = 2,   // entry: the creature entry
    };

    struct Pending
    {
        FirstKind kind;
        uint32 entry;
        TeamId team;
        ObjectGuid guid;
        std::string name;
        std::string guild;
        std::string what;  // boss name
    };

    bool g_enabled = false;
    std::mutex g_lock;
    std::vector<Pending> g_pending;
    std::set<std::tuple<uint8, uint32, uint8>> g_taken;
    bool g_loaded = false;

    std::string GuildOf(Player* player)
    {
        if (!player->GetGuildId())
            return {};
        Guild* guild = sGuildMgr->GetGuildById(player->GetGuildId());
        return guild ? guild->GetName() : std::string();
    }

    void Queue(Pending p)
    {
        std::lock_guard<std::mutex> lock(g_lock);
        g_pending.push_back(std::move(p));
    }

    void Load()
    {
        g_taken.clear();
        if (QueryResult result = PlayerbotsDatabase.Query("SELECT `kind`, `entry`, `team` FROM `coa_world_server_first`"))
        {
            do
            {
                Field* f = result->Fetch();
                g_taken.emplace(f[0].Get<uint8>(), f[1].Get<uint32>(), f[2].Get<uint8>());
            } while (result->NextRow());
        }

        // An older realm: a side with characters at the top level already had its first.
        uint32 top = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
        if (QueryResult result = CharacterDatabase.Query("SELECT DISTINCT `race` FROM `characters` WHERE `level` >= {}", top))
        {
            do
            {
                uint8 race = result->Fetch()[0].Get<uint8>();
                TeamId team = Player::TeamIdForRace(race);
                if (team > TEAM_HORDE || g_taken.count({ FIRST_LEVEL, top, uint8(team) }))
                    continue;
                g_taken.emplace(FIRST_LEVEL, top, uint8(team));
                PlayerbotsDatabase.Execute("INSERT IGNORE INTO `coa_world_server_first` (`kind`, `entry`, `team`, `guid`, `name`, "
                    "`guild`, `at`) VALUES ({}, {}, {}, 0, '', '', UNIX_TIMESTAMP())", uint32(FIRST_LEVEL), top, uint32(team));
            } while (result->NextRow());
        }
        g_loaded = true;
    }

    void Handle()
    {
        std::vector<Pending> pending;
        {
            std::lock_guard<std::mutex> lock(g_lock);
            pending.swap(g_pending);
        }

        for (Pending& p : pending)
        {
            auto key = std::make_tuple(uint8(p.kind), p.entry, uint8(p.team));
            if (g_taken.count(key))
                continue;
            g_taken.insert(key);

            std::string name = p.name, guild = p.guild;
            CharacterDatabase.EscapeString(name);
            CharacterDatabase.EscapeString(guild);
            PlayerbotsDatabase.Execute("INSERT IGNORE INTO `coa_world_server_first` (`kind`, `entry`, `team`, `guid`, `name`, "
                "`guild`, `at`) VALUES ({}, {}, {}, {}, '{}', '{}', UNIX_TIMESTAMP())", uint32(p.kind), p.entry,
                uint32(p.team), p.guid.GetCounter(), name, guild);

            char const* side = p.team == TEAM_ALLIANCE ? "the Alliance" : "the Horde";
            std::string who = p.guild.empty() ? p.name : Acore::StringFormat("{} of <{}>", p.name, p.guild);
            std::string text = p.kind == FIRST_LEVEL
                ? Acore::StringFormat("|cffff8000[Server First]|r {} is the first of {} to reach level {}!", who, side, p.entry)
                : Acore::StringFormat("|cffff8000[Server First]|r {} has slain {}, a first for {}!", who, p.what, side);
            ChatHandler(nullptr).SendWorldText(text);
            LOG_INFO("module", "CoA World: server first ({}): {}", side, text);
            CoaWorld::LogEvent("server_first", p.name, p.kind == FIRST_LEVEL
                ? Acore::StringFormat("first of {} to level {}", side, p.entry)
                : Acore::StringFormat("first kill of {} for {}", p.what, side));
        }
    }
}

class CoaWorldServerFirstPlayerScript : public PlayerScript
{
public:
    CoaWorldServerFirstPlayerScript()
        : PlayerScript("CoaWorldServerFirstPlayerScript", { PLAYERHOOK_ON_LEVEL_CHANGED, PLAYERHOOK_ON_CREATURE_KILL })
    {
    }

    void OnPlayerLevelChanged(Player* player, uint8 oldLevel) override
    {
        if (!g_enabled || !player || player->IsGameMaster())
            return;
        uint32 top = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
        if (player->GetLevel() < top || oldLevel >= top)
            return;
        Queue({ FIRST_LEVEL, top, player->GetTeamId(), player->GetGUID(), player->GetName(), GuildOf(player), {} });
    }

    // The raid boss falls: the first of the killer's side, told with the name of the raid leader.
    void OnPlayerCreatureKill(Player* killer, Creature* killed) override
    {
        if (!g_enabled || !killer || !killed || killer->IsGameMaster())
            return;
        Map* map = killed->GetMap();
        if (!map || !map->IsRaid())
            return;
        if (!killed->IsDungeonBoss() && killed->GetCreatureTemplate()->rank != CREATURE_ELITE_WORLDBOSS)
            return;

        Player* leader = killer;
        if (Group* group = killer->GetGroup())
            if (Player* l = ObjectAccessor::FindConnectedPlayer(group->GetLeaderGUID()))
                leader = l;
        Queue({ FIRST_BOSS, killed->GetEntry(), killer->GetTeamId(), leader->GetGUID(), leader->GetName(), GuildOf(leader),
            killed->GetName() });
    }
};

class CoaWorldServerFirstWorldScript : public WorldScript
{
public:
    CoaWorldServerFirstWorldScript()
        : WorldScript("CoaWorldServerFirstWorldScript", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE })
    {
    }

    void OnAfterConfigLoad(bool reload) override
    {
        if (reload)
            Read();
    }

    void OnStartup() override { Read(); }

    void OnUpdate(uint32 diff) override
    {
        if (!g_enabled)
            return;
        _timer += diff;
        if (_timer < 2 * IN_MILLISECONDS)
            return;
        _timer = 0;
        Handle();
    }

private:
    static void Read()
    {
        g_enabled = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaWorld.ServerFirst.Enable", false, false);
        if (!g_enabled || g_loaded)
            return;
        if (!TableReady())
        {
            g_enabled = false;
            return;
        }
        Load();
    }

    // The core aborts the server on a query naming a missing table or column (errors 1146/1054): check
    // coa_world_server_first first through information_schema, which never fails, and stay off without it.
    static bool TableReady()
    {
        QueryResult r = PlayerbotsDatabase.Query(
            "SELECT COUNT(*) FROM information_schema.columns WHERE table_schema = DATABASE() AND table_name = "
            "'coa_world_server_first' AND column_name IN ('kind','entry','team','guid','name','guild','at')");
        if (r && r->Fetch()[0].Get<uint64>() == 7)
            return true;
        LOG_ERROR("server.loading", ">> CoA World ServerFirst: table coa_world_server_first missing or out of date "
                                    "(data/sql/playerbots/custom not applied): turned off, the server starts without it.");
        return false;
    }

    uint32 _timer = 0;
};

void AddSC_coa_world_server_first()
{
    new CoaWorldServerFirstPlayerScript();
    new CoaWorldServerFirstWorldScript();
}
