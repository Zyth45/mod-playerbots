/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

/*
 * CoA World: simulated dungeon runs of random bots.
 *
 * When no real player is waiting in the Dungeon Finder, free random bots of a faction get together
 * for a classic dungeon of their level: the leader calls in LookingForGroup, the five gather at the
 * entrance (walking, or brought out of sight of real players), and step through the portal: they
 * log out there. Nobody sees inside another group's instance, so no instance is loaded and no CPU
 * is spent while they are "inside". After a believable time (the median clear time of Dungeon
 * Clear's bench runs, with some spread) they log back in at the entrance with the experience of the
 * run and the real loot of the bosses they killed, rolled from the bosses' own loot tables at the
 * usual chances. One of them says a word about it.
 *
 * The idea of bots creating Dungeon Finder demand comes from Wilhelm8585's mod-playerbots fork
 * (GPL-2.0, autonomy v0.12); this is a different design, decided for CoA (simulated run, no queue,
 * no group object, no instance).
 *
 * Bosses and times come from the CoA data (coa-local MCP: world DB 2.9.3, DC test runs to
 * 08/11/2026). World thread only. Config: AiPlayerbot.CoaWorld.DungeonSim.*, off by default.
 */

#include "CoaWorldBots.h"
#include "CoaWorldState.h"
#include "CoaWorldStateRules.h"

#include "Chat.h"
#include "ChatCommand.h"
#include "Config.h"
#include "DBCStores.h"
#include "Formulas.h"
#include "Item.h"
#include "LFGMgr.h"
#include "Log.h"
#include "LootMgr.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include "World.h"
#include "WorldSession.h"

#include <algorithm>
#include <deque>
#include <random>
#include <unordered_map>
#include <vector>

using namespace Acore::ChatCommands;

namespace
{
    using namespace CoaWorld;

    struct DungeonDef
    {
        uint32 mapId;
        char const* name;
        uint8 minLevel;          // bots of this level band go
        uint8 maxLevel;
        uint32 medianMinutes;    // median clear time of the DC bench (normal)
        std::vector<uint32> bosses;
    };

    // Classic dungeons of CoA: bosses (level 63 and below) and median clear times from coa-local.
    std::vector<DungeonDef> const DUNGEONS = {
        { 389, "Ragefire Chasm", 13, 16, 27, { 11517, 11518, 11519, 11520 } },
        { 36, "Deadmines", 16, 21, 25, { 644, 3586, 642, 645, 646, 647, 1763, 639 } },
        { 43, "Wailing Caverns", 17, 21, 47, { 3653, 3669, 3671, 5912, 3670, 3673, 3674, 5775 } },
        { 34, "Stormwind Stockade", 21, 29, 19, { 1696, 1663, 1720, 1666, 1717, 1716 } },
        { 33, "Shadowfang Keep", 17, 26, 26, { 3914, 3886, 3887, 4278, 4279, 3872, 3927, 4274, 14682, 4275, 10000 } },
        { 48, "Blackfathom Deeps", 22, 28, 39, { 4831, 4887, 4830, 6243, 12902, 4832, 4829 } },
        { 90, "Gnomeregan", 27, 34, 29, { 7079, 6229, 6235, 6228, 7800 } },
        { 47, "Razorfen Kraul", 25, 33, 46, { 6168, 4424, 4428, 4420, 4425, 4842, 4421, 4422 } },
        { 129, "Razorfen Downs", 34, 41, 29, { 7356, 7357, 7354, 8567, 14686, 7358 } },
        { 189, "Scarlet Monastery", 29, 42, 14, { 3983, 6488, 6489, 6490, 3974, 4543, 14693, 6487, 3975, 4542, 3976, 3977 } },
        { 70, "Uldaman", 37, 47, 26, { 6910, 7228, 6906, 7023, 7206, 4854, 7291, 2748 } },
        { 209, "Zul'Farrak", 42, 48, 27, { 7604, 10080, 10082, 7271, 7272, 7795, 7797, 10081, 7267, 8127 } },
        { 349, "Maraudon", 44, 51, 40, { 12236, 12237, 12258, 13282, 12225, 12203, 13596, 13601, 12201 } },
        { 109, "Sunken Temple", 47, 55, 47, { 8580, 5708, 5712, 5720, 5716, 5717, 5719, 5711, 5721, 5722, 5710, 5709 } },
        { 230, "Blackrock Depths", 48, 59, 60, { 9025, 9018, 9024, 9319, 9543, 9016, 9056, 9017, 9034, 9035, 9042, 9499, 9502, 9537, 9036, 9037, 9038, 9040, 9041, 8923, 8983, 9033, 9039, 9156, 9938, 8929, 9019 } },
        { 229, "Blackrock Spire", 54, 60, 40, { 9219, 10263, 9217, 9218, 9236, 9196, 9237, 9596, 9718, 9736, 10220, 10509, 10596, 9568, 9816, 10376, 10899, 10363, 10429, 10430, 10162 } },
        { 429, "Dire Maul", 53, 60, 23, { 14349, 11490, 13280, 14327, 14354, 11491, 11492, 14241, 11467, 14321, 14322, 14323, 14326, 11487, 11488, 11489, 14308, 14324, 11486, 11496, 14325, 11501 } },
        { 289, "Scholomance", 55, 60, 43, { 10433, 10432, 10502, 10504, 10505, 10507, 10901, 11261, 1853, 10503, 11622, 14861, 10508 } },
        { 329, "Stratholme", 54, 60, 46, { 10516, 10558, 11082, 10393, 10435, 10808, 10436, 10437, 10809, 10811, 10917, 10997, 11032, 10438, 10440, 10812 } },
    };

    struct Settings
    {
        bool enabled = false;
        uint32 groupsPerFaction = 2;
        uint32 factionIntervalSeconds = 600;
        uint32 botCooldownMinutes = 120;
        uint32 gatherMinutes = 6;
        uint32 durationPct = 100;
        uint32 durationSpreadPct = 25;
        uint32 xpPct = 100;
        bool announce = true;
    };

    Settings settings;

    constexpr char const* PREFIX = "AiPlayerbot.CoaWorld.DungeonSim.";
    constexpr uint32 CHECK_MS = 30 * IN_MILLISECONDS;
    constexpr uint32 RETURN_WAIT_SECONDS = 15 * MINUTE;
    constexpr float AT_ENTRANCE = 25.0f;

    enum class Phase : uint8
    {
        Gathering,
        Inside,
        Returning,
    };

    struct Reward
    {
        uint32 xp = 0;
        uint32 money = 0;
        std::vector<std::pair<uint32, uint32>> items;  // item, count
        bool given = false;
    };

    struct Run
    {
        uint32 id = 0;
        TeamId team = TEAM_ALLIANCE;
        DungeonDef const* dungeon = nullptr;
        WorldLocation entrance;
        std::vector<ObjectGuid> members;
        Phase phase = Phase::Gathering;
        time_t until = 0;
        bool rolled = false;
        bool spoke = false;
        std::vector<uint32> killed;
        std::unordered_map<ObjectGuid, Reward> rewards;
        uint32 level = 0;  // the group's mean level
        time_t startedAt = 0, enteredAt = 0, insideUntil = 0;

        uint32 Owner() const { return BOT_OWNER_DUNGEON_SIM_BASE + id; }
    };


    // The dungeon's name as the bots say it (the French client's names).
    std::string DungeonName(DungeonDef const& d, bool fr)
    {
        if (!fr)
            return d.name;
        switch (d.mapId)
        {
            case 389: return "Gouffre de Ragefeu";
            case 36: return "Mortemines";
            case 43: return "Cavernes des lamentations";
            case 34: return "Prison de Hurlevent";
            case 33: return "Donjon d'Ombrecroc";
            case 48: return "Profondeurs de Brassenoire";
            case 47: return "Kraal de Tranchebauge";
            case 129: return "Souilles de Tranchebauge";
            case 189: return "Monast\u00e8re \u00e9carlate";
            case 109: return "Temple englouti";
            case 230: return "Profondeurs de Rochenoire";
            case 229: return "Pic Rochenoire";
            case 429: return "Hache-Tripes";
            default: return d.name;  // Gnomeregan, Uldaman, Zul'Farrak, Maraudon, Scholomance, Stratholme
        }
    }

    bool IsHumanPlayer(Player* player)
    {
        return player && player->IsInWorld() && !GET_PLAYERBOT_AI(player) && player->GetSession() &&
               !player->GetSession()->IsBot();
    }

    class DungeonSimManager
    {
    public:
        static DungeonSimManager& Instance()
        {
            static DungeonSimManager instance;
            return instance;
        }

        bool Active() const { return !_runs.empty(); }

        void Update(uint32 diff)
        {
            _checkTimer += diff;
            if (_checkTimer < CHECK_MS)
                return;
            _checkTimer = 0;

            time_t now = time(nullptr);
            for (auto itr = _runs.begin(); itr != _runs.end();)
            {
                if (UpdateRun(*itr, now))
                    ++itr;
                else
                    itr = _runs.erase(itr);
            }

            if (!settings.enabled || HumansQueued())
                return;

            for (TeamId team : { TEAM_ALLIANCE, TEAM_HORDE })
                if (RunsOf(team) < settings.groupsPerFaction && now >= _nextStart[team])
                    if (TryStart(team, nullptr, now))
                        _nextStart[team] = now + settings.factionIntervalSeconds;
        }

        // GM start: one run for that side now, at that dungeon if given.
        bool ForceStart(TeamId team, DungeonDef const* dungeon)
        {
            return TryStart(team, dungeon, time(nullptr));
        }

        std::string Status() const
        {
            std::string out = Acore::StringFormat("{} simulated dungeon run(s){}.", _runs.size(),
                settings.enabled ? "" : " (switched off)");
            time_t now = time(nullptr);
            for (Run const& run : _runs)
                out += Acore::StringFormat(" [{} {} {}, {} bots, {}]", run.team == TEAM_ALLIANCE ? "A" : "H",
                    run.dungeon->name,
                    run.phase == Phase::Gathering ? "gathering" : run.phase == Phase::Inside ? "inside" : "back",
                    run.members.size(),
                    run.until > now ? Acore::StringFormat("{} min", (run.until - now + 59) / 60) : std::string("-"));
            return out;
        }

        // Ends every run: the bots inside come back on the next pass of the random bot manager.
        void StopAll()
        {
            for (Run& run : _runs)
            {
                ReleaseBots(run.Owner(), true);
                for (ObjectGuid const& guid : run.members)
                    ReleaseHold(guid.GetCounter());
            }
            _runs.clear();
        }

    private:
        uint32 RunsOf(TeamId team) const
        {
            uint32 n = 0;
            for (Run const& run : _runs)
                if (run.team == team)
                    ++n;
            return n;
        }

        // A real player waiting in the Dungeon Finder: the bots fill that queue instead
        // (RandomPlayerbotMgr::CheckLfgQueue), as before.
        static bool HumansQueued()
        {
            for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
            {
                if (!IsHumanPlayer(player))
                    continue;
                ObjectGuid id = player->GetGroup() ? player->GetGroup()->GetGUID() : player->GetGUID();
                lfg::LfgState state = sLFGMgr->GetState(id);
                if (state != lfg::LFG_STATE_NONE && state < lfg::LFG_STATE_DUNGEON)
                    return true;
            }
            return false;
        }

        bool OnCooldown(Player* bot, time_t now) const
        {
            auto itr = _cooldown.find(bot->GetGUID().GetCounter());
            return itr != _cooldown.end() && itr->second > now;
        }

        bool TryStart(TeamId team, DungeonDef const* only, time_t now)
        {
            std::vector<Player*> free;
            for (auto const& [guid, bot] : RandomBots())
                if (bot && bot->GetTeamId() == team && IsFreeBot(bot) && !OnCooldown(bot, now) &&
                    !IsHeld(bot->GetGUID().GetCounter()))
                    free.push_back(bot);
            if (free.size() < 5)
                return false;

            std::vector<DungeonDef const*> order;
            for (DungeonDef const& d : DUNGEONS)
                if (!only || &d == only)
                    order.push_back(&d);
            std::shuffle(order.begin(), order.end(), std::mt19937(urand(0, 0x7FFFFFFF)));

            for (DungeonDef const* dungeon : order)
            {
                // The spot outside the portal, where leaving the dungeon puts you.
                AreaTriggerTeleport const* door = sObjectMgr->GetGoBackTrigger(dungeon->mapId);
                if (!door)
                    continue;
                WorldLocation entrance(door->target_mapId, door->target_X, door->target_Y, door->target_Z,
                    door->target_Orientation);

                std::vector<Player*> group = PickGroup(free, *dungeon, entrance);
                if (group.size() < 5)
                    continue;

                Run run;
                run.id = ++_lastId;
                run.team = team;
                run.dungeon = dungeon;
                run.entrance = entrance;
                run.until = now + settings.gatherMinutes * MINUTE;
                run.startedAt = now;
                for (Player* bot : group)
                    run.level += bot->GetLevel();
                run.level /= uint32(group.size());

                BorrowRequest request;
                request.mapId = entrance.GetMapId();
                request.goal = entrance;
                request.rally = entrance;
                request.nearRadius = 400.0f;
                request.teleport = true;
                for (Player* bot : group)
                    if (SendBot(run.Owner(), bot, request))
                        run.members.push_back(bot->GetGUID());

                if (run.members.size() < 3)
                {
                    ReleaseBots(run.Owner(), true);
                    continue;
                }

                if (settings.announce)
                    Announce(run, group.front());

                LOG_INFO("module", "CoA World: simulated {} run for {} ({} bots gathering)", dungeon->name,
                    team == TEAM_ALLIANCE ? "the Alliance" : "the Horde", run.members.size());
                _runs.push_back(std::move(run));
                return true;
            }
            return false;
        }

        // One tank, one healer, three others, all of the dungeon's level; the nearest to the door first.
        std::vector<Player*> PickGroup(std::vector<Player*> const& free, DungeonDef const& dungeon,
                                       WorldLocation const& entrance) const
        {
            std::vector<Player*> fit;
            for (Player* bot : free)
                if (bot->GetLevel() >= dungeon.minLevel && bot->GetLevel() <= dungeon.maxLevel)
                    fit.push_back(bot);
            if (fit.size() < 5)
                return {};

            std::sort(fit.begin(), fit.end(), [&entrance](Player* a, Player* b)
            {
                float da = a->GetMapId() == entrance.GetMapId() ? a->GetExactDist(&entrance) : 1e9f;
                float db = b->GetMapId() == entrance.GetMapId() ? b->GetExactDist(&entrance) : 1e9f;
                return da < db;
            });

            Player* tank = nullptr;
            Player* healer = nullptr;
            std::vector<Player*> others;
            for (Player* bot : fit)
            {
                bool isTank = PlayerbotAI::IsTank(bot, true);
                bool isHeal = PlayerbotAI::IsHeal(bot, true);
                if (!tank && isTank)
                    tank = bot;
                else if (!healer && isHeal)
                    healer = bot;
                else if (!isTank && !isHeal && others.size() < 3)
                    others.push_back(bot);
            }
            if (!tank || !healer || others.size() < 3)
                return {};

            std::vector<Player*> out = { tank, healer };
            out.insert(out.end(), others.begin(), others.end());
            return out;
        }

        // (Lines to move to IA MIND's phrase bank.)
        void Announce(Run const& run, Player* leader)
        {
            PlayerbotAI* botAI = GET_PLAYERBOT_AI(leader);
            if (!botAI)
                return;
            static std::vector<char const*> const lines = {
                "LFM {}, got tank and heal, meeting at the stone",
                "{} run forming, all roles in, heading there now",
                "anyone up for {}? group's full, going in a few",
                "{} with a full group, see you inside",
            };
            static std::vector<char const*> const linesFr = {
                "LFM {}, on a tank et heal, rdv \u00e0 la pierre",
                "groupe {} mont\u00e9, tous les r\u00f4les sont l\u00e0, on y va",
                "qui pour {} ? groupe complet, on part dans pas longtemps",
                "{} avec un groupe complet, \u00e0 tout' dedans",
            };
            bool const fr = BotsFrench();
            auto const& pool = fr ? linesFr : lines;
            std::string line = Acore::StringFormat(pool[urand(0, pool.size() - 1)], DungeonName(*run.dungeon, fr));
            // The bank's line when it has one.
            if (!BotLine(leader, "dsim_lfm", { { "dungeon", run.dungeon->name }, { "need", "1 dps" } }) && BotChatOn())
                botAI->SayToChannel(line, LOOKING_FOR_GROUP);
        }

        // false: the run is over.
        bool UpdateRun(Run& run, time_t now)
        {
            switch (run.phase)
            {
                case Phase::Gathering:
                {
                    uint32 there = 0;
                    uint32 online = 0;
                    for (ObjectGuid const& guid : run.members)
                    {
                        Player* bot = ObjectAccessor::FindPlayer(guid);
                        if (!bot || !bot->IsInWorld())
                            continue;
                        ++online;
                        if (bot->GetMapId() == run.entrance.GetMapId() && bot->GetExactDist(&run.entrance) < AT_ENTRANCE)
                            ++there;
                    }
                    if (online < 3)
                    {
                        ReleaseBots(run.Owner(), true);
                        return false;
                    }
                    if (there < online && now < run.until)
                        return true;
                    Enter(run, now);
                    return !run.members.empty();
                }
                case Phase::Inside:
                    if (now < run.until)
                        return true;
                    // Out again: RandomPlayerbotMgr logs them back in at the door.
                    for (ObjectGuid const& guid : run.members)
                        ReleaseHold(guid.GetCounter());
                            run.phase = Phase::Returning;
                    run.until = now + RETURN_WAIT_SECONDS;
                    return true;
                case Phase::Returning:
                    return Return(run, now);
            }
            return false;
        }

        // Those at the door go in, and those still on their way when time is up if nobody sees them.
        void Enter(Run& run, time_t now)
        {
            uint32 minutes = std::max<uint32>(5, run.dungeon->medianMinutes * settings.durationPct / 100);
            int32 spread = int32(minutes * settings.durationSpreadPct / 100);
            minutes = uint32(std::max<int32>(5, int32(minutes) + irand(-spread, spread)));

            std::vector<ObjectGuid> inside;
            for (ObjectGuid const& guid : run.members)
            {
                Player* bot = ObjectAccessor::FindPlayer(guid);
                if (!bot || !bot->IsInWorld() || bot->IsInCombat() || !bot->IsAlive())
                    continue;
                PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
                bool atDoor = bot->GetMapId() == run.entrance.GetMapId() && bot->GetExactDist(&run.entrance) < AT_ENTRANCE;
                if (!botAI || (!atDoor && botAI->HasPlayerNearby(150.0f)))
                    continue;
                inside.push_back(guid);
            }

            // Who is not going in goes back to its usual life.
            for (ObjectGuid const& guid : run.members)
                if (std::find(inside.begin(), inside.end(), guid) == inside.end())
                    if (Player* bot = ObjectAccessor::FindPlayer(guid))
                        if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
                            botAI->rpgInfo.ChangeToIdle();
            ReleaseBots(run.Owner(), inside.size() < 3);

            if (inside.size() < 3)
            {
                run.members.clear();
                return;
            }

            for (ObjectGuid const& guid : inside)
            {
                Player* bot = ObjectAccessor::FindPlayer(guid);
                if (!bot)
                    continue;
                if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
                    botAI->rpgInfo.ChangeToIdle();
                // Through the portal: logged out at the door, where it comes back. A bot still on its
                // way (unseen, checked above) is put there first.
                if (bot->GetMapId() != run.entrance.GetMapId() || bot->GetExactDist(&run.entrance) >= AT_ENTRANCE)
                    bot->TeleportTo(run.entrance);
                HoldBot(guid.GetCounter(), now + minutes * MINUTE);
                    sRandomPlayerbotMgr.LogoutPlayerBot(guid);
            }

            // The bosses of this run: all of a small dungeon, some of a big one.
            std::vector<uint32> bosses = run.dungeon->bosses;
            std::shuffle(bosses.begin(), bosses.end(), std::mt19937(urand(0, 0x7FFFFFFF)));
            if (bosses.size() > 7)
                bosses.resize(urand(4, 7));
            run.killed = bosses;

            run.members = inside;
            run.phase = Phase::Inside;
            run.until = now + minutes * MINUTE;
            run.enteredAt = now;
            run.insideUntil = run.until;
            LOG_INFO("module", "CoA World: {} bots entered {} for {} minutes", inside.size(), run.dungeon->name, minutes);
        }

        // Back at the door: the experience, money and loot of the run, rolled once from the bosses'
        // own tables (the first one back holds the roll), then handed to each as they log in.
        bool Return(Run& run, time_t now)
        {
            std::vector<Player*> back;
            for (ObjectGuid const& guid : run.members)
                if (Player* bot = ObjectAccessor::FindPlayer(guid))
                    if (bot->IsInWorld() && !bot->IsBeingTeleported())
                        back.push_back(bot);

            if (!run.rolled && !back.empty())
            {
                Roll(run, back.front());
                Record(run, now);
            }

            for (Player* bot : back)
                Give(run, bot);

            if (!run.spoke && !back.empty())
            {
                run.spoke = true;
                Speak(run, back[urand(0, back.size() - 1)]);
            }

            bool everyone = std::all_of(run.members.begin(), run.members.end(), [&run](ObjectGuid const& guid)
            {
                auto itr = run.rewards.find(guid);
                return itr != run.rewards.end() && itr->second.given;
            });
            if (everyone || now >= run.until)
            {
                for (ObjectGuid const& guid : run.members)
                    _cooldown[guid.GetCounter()] = now + settings.botCooldownMinutes * MINUTE;
                return false;
            }
            return true;
        }

        void Roll(Run& run, Player* holder)
        {
            run.rolled = true;
            ContentLevels content = GetContentLevelsForMapAndZone(run.dungeon->mapId, 0);
            uint32 money = 0;
            uint32 bossLevelSum = 0;
            std::vector<std::pair<uint32, uint32>> items;

            for (uint32 entry : run.killed)
            {
                CreatureTemplate const* cInfo = sObjectMgr->GetCreatureTemplate(entry);
                if (!cInfo)
                    continue;
                bossLevelSum += cInfo->maxlevel;
                money += urand(cInfo->mingold, std::max(cInfo->mingold, cInfo->maxgold));
                if (!cInfo->lootid)
                    continue;

                Loot loot;
                loot.FillLoot(cInfo->lootid, LootTemplates_Creature, holder, false, true);
                for (LootItem const& item : loot.items)
                    if (item.itemid && sObjectMgr->GetItemTemplate(item.itemid))
                        items.emplace_back(item.itemid, std::max<uint32>(1, item.count));
            }

            // Experience: each boss as an elite kill, plus the trash between bosses (about four elite
            // kills per boss), shared by five as the core shares a group's kills.
            uint32 kills = run.killed.empty() ? 1 : uint32(run.killed.size());
            uint8 bossLevel = uint8(bossLevelSum / kills);
            uint32 share = std::max<uint32>(1, uint32(run.members.size()));
            for (ObjectGuid const& guid : run.members)
            {
                Reward& reward = run.rewards[guid];
                reward.money = money / share;
                Player* bot = ObjectAccessor::FindPlayer(guid);
                uint8 level = bot ? bot->GetLevel() : bossLevel;
                uint32 perKill = Acore::XP::BaseGain(level, bossLevel, content) * 2;
                reward.xp = perKill * kills * 5 / share * settings.xpPct / 100;
            }

            // Each piece to one member, as the rolls would have done.
            for (auto const& item : items)
                run.rewards[run.members[urand(0, run.members.size() - 1)]].items.push_back(item);
        }

        // For monde.json: today's totals and the last runs, counted when the loot is rolled.
        void Record(Run const& run, time_t now)
        {
            int64 const since = CoaWorldState::TodaySince(now);
            if (_today.since != since)
                _today = Today{ since };
            ++_today.runs;
            _today.bossKills += uint32(run.killed.size());
            uint32 items = 0;
            for (auto const& [guid, reward] : run.rewards)
                items += uint32(reward.items.size());
            _today.items += items;
            _last.push_front({ run.dungeon, uint32(run.members.size()), uint32(run.insideUntil - run.enteredAt),
                               uint32(run.killed.size()), now });
            if (_last.size() > 8)
                _last.pop_back();
        }

    public:
        std::string Json(time_t now)
        {
            if (!settings.enabled && _runs.empty())
                return "null";
            using CoaWorldStateRules::Quote;
            std::string running = "[";
            for (Run const& run : _runs)
            {
                char const* phase = run.phase == Phase::Gathering ? "gathering" : run.phase == Phase::Inside ? "inside" : "back";
                time_t const ends = run.phase == Phase::Gathering ? 0 : run.insideUntil;
                running += std::string(running.size() > 1 ? "," : "") + "{\"dungeon\":" + std::to_string(run.dungeon->mapId) +
                           ",\"dungeonName\":" + Quote(run.dungeon->name) +
                           ",\"faction\":" + (run.team == TEAM_ALLIANCE ? "\"alliance\"" : "\"horde\"") +
                           ",\"level\":" + std::to_string(run.level) + ",\"bots\":" + std::to_string(run.members.size()) +
                           ",\"phase\":\"" + phase + "\",\"startedAt\":" + std::to_string(int64(run.startedAt)) +
                           ",\"endsAt\":" + CoaWorldStateRules::DateOrNull(ends) + ",\"bosses\":" + std::to_string(run.killed.size()) + "}";
            }
            int64 const since = CoaWorldState::TodaySince(now);
            if (_today.since != since)
                _today = Today{ since };
            std::string last = "[";
            for (Done const& d : _last)
                last += std::string(last.size() > 1 ? "," : "") + "{\"dungeon\":" + std::to_string(d.dungeon->mapId) +
                        ",\"dungeonName\":" + Quote(d.dungeon->name) + ",\"bots\":" + std::to_string(d.bots) +
                        ",\"durationS\":" + std::to_string(d.durationS) + ",\"bossesKilled\":" + std::to_string(d.bosses) +
                        ",\"at\":" + std::to_string(int64(d.at)) + "}";
            return "{\"running\":" + running + "],\"today\":{\"since\":" + std::to_string(_today.since) +
                   ",\"runs\":" + std::to_string(_today.runs) + ",\"bossKills\":" + std::to_string(_today.bossKills) +
                   ",\"items\":" + std::to_string(_today.items) + "},\"last\":" + last + "]}";
        }

    private:
        struct Today
        {
            int64 since = 0;
            uint32 runs = 0, bossKills = 0, items = 0;
        };
        struct Done
        {
            DungeonDef const* dungeon;
            uint32 bots, durationS, bosses;
            time_t at;
        };
        Today _today;
        std::deque<Done> _last;

        void Give(Run& run, Player* bot)
        {
            auto itr = run.rewards.find(bot->GetGUID());
            if (itr == run.rewards.end() || itr->second.given)
                return;
            Reward& reward = itr->second;
            reward.given = true;

            if (reward.xp && bot->GetLevel() < sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
                bot->GiveXP(reward.xp, nullptr);
            if (reward.money)
                bot->ModifyMoney(int32(reward.money));
            for (auto const& [itemId, count] : reward.items)
            {
                ItemPosCountVec dest;
                if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemId, count) == EQUIP_ERR_OK)
                    bot->StoreNewItem(dest, itemId, true);
            }
        }

        // One word on the way out: pleased with a good drop, tired otherwise. To guild chat when the
        // bot has a guild, else aloud at the door. (Lines to move to IA MIND's phrase bank.)
        void Speak(Run const& run, Player* bot)
        {
            PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
            if (!botAI)
                return;

            bool goodDrop = false;
            for (auto const& [guid, reward] : run.rewards)
                for (auto const& [itemId, count] : reward.items)
                    if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId))
                        goodDrop |= proto->Quality >= ITEM_QUALITY_RARE;

            static std::vector<char const*> const happy = { "finally a drop from {}", "gg, {} was generous today",
                "{} done, and a blue at last", "good {} run, ty all" };
            static std::vector<char const*> const plain = { "gg", "{} done, gg all", "ty for the {} run",
                "{} cleared, nothing for me again", "gg, that was a long {}" };
            static std::vector<char const*> const happyFr = { "enfin un drop sur {}", "gg, {} a \u00e9t\u00e9 g\u00e9n\u00e9reux aujourd'hui",
                "{} fini, et enfin un bleu", "bon run {}, merci \u00e0 tous" };
            static std::vector<char const*> const plainFr = { "gg", "{} fini, gg \u00e0 tous", "merci pour le run {}",
                "{} clean, encore rien pour moi", "gg, c'\u00e9tait long {}" };
            bool const fr = BotsFrench();
            auto const& pool = goodDrop ? (fr ? happyFr : happy) : (fr ? plainFr : plain);
            std::string line = Acore::StringFormat(pool[urand(0, pool.size() - 1)], DungeonName(*run.dungeon, fr));
            // The bank's line first, with the run's best item for %item.
            std::string item;
            for (auto const& [guid, reward] : run.rewards)
                for (auto const& [itemId, count] : reward.items)
                    if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId))
                        if (proto->Quality >= ITEM_QUALITY_RARE)
                            item = proto->Name1;
            if (BotLine(bot, goodDrop ? "dsim_back_drop" : "dsim_back_plain", { { "dungeon", run.dungeon->name }, { "item", item } }))
                return;
            if (!BotChatOn())
                return;
            if (bot->GetGuildId())
                botAI->SayToGuild(line);
            else
                botAI->Say(line);
        }

        std::vector<Run> _runs;
        std::unordered_map<uint32, time_t> _cooldown;
        time_t _nextStart[2] = { 0, 0 };
        uint32 _checkTimer = 0;
        uint32 _lastId = 0;
    };

    DungeonSimManager& Sim() { return DungeonSimManager::Instance(); }

    void LoadSettings()
    {
        auto key = [](char const* name) { return std::string(PREFIX) + name; };
        settings.enabled = sConfigMgr->GetOption<bool>(key("Enable"), true, false);
        settings.groupsPerFaction = std::min<uint32>(10, sConfigMgr->GetOption<uint32>(key("GroupsPerFaction"), 2, false));
        settings.factionIntervalSeconds = sConfigMgr->GetOption<uint32>(key("FactionIntervalSeconds"), 600, false);
        settings.botCooldownMinutes = sConfigMgr->GetOption<uint32>(key("BotCooldownMinutes"), 120, false);
        settings.gatherMinutes = std::clamp<uint32>(sConfigMgr->GetOption<uint32>(key("GatherMinutes"), 6, false), 1, 30);
        settings.durationPct = std::clamp<uint32>(sConfigMgr->GetOption<uint32>(key("DurationPct"), 100, false), 20, 500);
        settings.durationSpreadPct = std::min<uint32>(90, sConfigMgr->GetOption<uint32>(key("DurationSpreadPct"), 25, false));
        settings.xpPct = std::min<uint32>(1000, sConfigMgr->GetOption<uint32>(key("XpPct"), 100, false));
        settings.announce = sConfigMgr->GetOption<bool>(key("Announce"), true, false);
    }

    DungeonDef const* FindDungeon(std::string name)
    {
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        for (DungeonDef const& d : DUNGEONS)
        {
            std::string dn = d.name;
            std::transform(dn.begin(), dn.end(), dn.begin(), ::tolower);
            if (dn.find(name) != std::string::npos)
                return &d;
        }
        return nullptr;
    }
}

class CoaWorldDungeonSimWorldScript : public WorldScript
{
public:
    CoaWorldDungeonSimWorldScript()
        : WorldScript("CoaWorldDungeonSimWorldScript", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE })
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
        // Switched off with runs going on: they come back on their own.
        if (settings.enabled || Sim().Active())
            Sim().Update(diff);
    }
};

class CoaWorldDungeonSimCommandScript : public CommandScript
{
public:
    CoaWorldDungeonSimCommandScript() : CommandScript("CoaWorldDungeonSimCommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable simTable = {
            { "status", HandleStatus, SEC_GAMEMASTER, Console::Yes },
            { "start",  HandleStart,  SEC_GAMEMASTER, Console::Yes },
            { "stop",   HandleStop,   SEC_GAMEMASTER, Console::Yes },
        };
        static ChatCommandTable worldTable = {
            { "dungeon", simTable },
        };
        static ChatCommandTable commandTable = {
            { "coaworld", worldTable },
        };
        return commandTable;
    }

    static bool HandleStatus(ChatHandler* handler)
    {
        handler->SendSysMessage(Sim().Status());
        return true;
    }

    // .coaworld dungeon start <alliance|horde> [part of a dungeon name]
    static bool HandleStart(ChatHandler* handler, std::string side, Optional<Tail> name)
    {
        TeamId team = (side == "horde" || side == "h") ? TEAM_HORDE : TEAM_ALLIANCE;
        DungeonDef const* dungeon = nullptr;
        if (name && !name->empty())
        {
            dungeon = FindDungeon(std::string(*name));
            if (!dungeon)
            {
                handler->SendSysMessage("No such dungeon in the list.");
                handler->SetSentErrorMessage(true);
                return false;
            }
        }
        if (!settings.enabled || !Sim().ForceStart(team, dungeon))
        {
            handler->SendSysMessage("No run started: switched off (AiPlayerbot.CoaWorld.DungeonSim.Enable), or no five "
                                    "free bots of the level with a tank and a healer.");
            handler->SetSentErrorMessage(true);
            return false;
        }
        handler->SendSysMessage(Sim().Status());
        return true;
    }

    static bool HandleStop(ChatHandler* handler)
    {
        Sim().StopAll();
        handler->SendSysMessage(Sim().Status());
        return true;
    }
};

void AddSC_coa_world_dungeon_sim()
{
    new CoaWorldDungeonSimWorldScript();
    new CoaWorldDungeonSimCommandScript();
}

std::string CoaWorld::DungeonSimJson(time_t now) { return DungeonSimManager::Instance().Json(now); }
