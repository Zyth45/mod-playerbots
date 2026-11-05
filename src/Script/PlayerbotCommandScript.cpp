/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "BattleGroundTactics.h"
#include "Chat.h"
#include "CoaReborn.h"
#include "CoaSpecialization.h"
#include "CoaWildcard.h"
#include "CoaMissionAction.h"
#include "CoaMissionMgr.h"
#include "Config.h"
#include "GroupMgr.h"
#include "GuildTaskMgr.h"
#include "PerfMonitor.h"
#include "PlayerbotFactory.h"
#include "PlayerbotMgr.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"

#include <sstream>

using namespace Acore::ChatCommands;

char const* CoaBotsVersion();  // Playerbots.cpp

class playerbots_commandscript : public CommandScript
{
public:
    playerbots_commandscript() : CommandScript("playerbots_commandscript") {}

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable playerbotsDebugCommandTable = {
            {"bg", HandleDebugBGCommand, SEC_GAMEMASTER, Console::Yes},
        };

        static ChatCommandTable playerbotsWildcardCommandTable = {
            {"redraft", HandleWildcardRedraftCommand, SEC_GAMEMASTER, Console::No},
        };

        static ChatCommandTable playerbotsAccountCommandTable = {
            {"setKey", HandleSetSecurityKeyCommand, SEC_PLAYER, Console::No},
            {"link", HandleLinkAccountCommand, SEC_PLAYER, Console::No},
            {"linkedAccounts", HandleViewLinkedAccountsCommand, SEC_PLAYER, Console::No},
            {"unlink", HandleUnlinkAccountCommand, SEC_PLAYER, Console::No},
        };

        static ChatCommandTable playerbotsCommandTable = {
            {"bot", HandlePlayerbotCommand, SEC_PLAYER, Console::No},
            {"gtask", HandleGuildTaskCommand, SEC_GAMEMASTER, Console::Yes},
            {"pmon", HandlePerfMonCommand, SEC_GAMEMASTER, Console::Yes},
            {"rndbot", HandleRandomPlayerbotCommand, SEC_GAMEMASTER, Console::Yes},
            {"coa", HandleCoaRecruitCommand, SEC_PLAYER, Console::Yes},
            {"missiontest", HandleMissionTestCommand, SEC_ADMINISTRATOR, Console::Yes},
            {"version", HandleVersionCommand, SEC_PLAYER, Console::Yes},
            {"debug", playerbotsDebugCommandTable},
            {"account", playerbotsAccountCommandTable},
            {"wildcard", playerbotsWildcardCommandTable},
        };

        static ChatCommandTable commandTable = {
            {"playerbots", playerbotsCommandTable},
        };

        return commandTable;
    }

    // .playerbots version : the CoA Bots version and where its source is. The login message says the same,
    // but a host may have removed it; the GPLv2 still wants that message kept on public servers.
    static bool HandleVersionCommand(ChatHandler* handler)
    {
        handler->PSendSysMessage("CoA Bots v{} (mod-playerbots) https://github.com/Zyth45/mod-playerbots",
                                 CoaBotsVersion());
        return true;
    }

    static bool HandlePlayerbotCommand(ChatHandler* handler, char const* args)
    {
        return PlayerbotMgr::HandlePlayerbotMgrCommand(handler, args);
    }

    // .playerbots coa tank|heal|dps [class] : recruit a Conquest of Azeroth bot for that role, of
    // that class when one is named (".playerbots coa heal sun cleric").
    // .playerbots coa raid [size] [tanks] [healers] : fills a raid with bots (25: 2 tanks, 4 healers).
    // Players may build a raid too, within AiPlayerbot.CoaRaidPlayerMax raids at a time on the realm and one
    // every AiPlayerbot.CoaRaidPlayerCooldown minutes each; game masters have no limit and alone recruit one role.
    // .playerbots coa regear [level] : game masters only, gears again the random bots online of that level or
    // above (60 by default) to the quality the config gives them now.
    // .playerbots coa wcr [spec <0-2>|reload] : game masters only, the Warcraft Reborn tree of the selected bot, moved to
    // another tree; reload reads playerbots_coa_wcr_builds again.
    static bool HandleCoaRecruitCommand(ChatHandler* handler, char const* args)
    {
        Player* master = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        std::string const line = args ? args : "";
        std::size_t const space = line.find(' ');
        std::string const wanted = line.substr(0, space);
        std::string const className = space == std::string::npos ? "" : line.substr(space + 1);

        // The server console (and SOAP) may regear the bots, nothing else: the rest needs a player to group with.
        bool const console = !handler->GetSession();
        if (console && wanted != "regear")
        {
            handler->SendSysMessage("From the console: .playerbots coa regear [level].");
            return true;
        }
        bool const gameMaster = console || handler->GetSession()->GetSecurity() >= SEC_GAMEMASTER;

        if (wanted == "raid")
        {
            if (!master)
                return false;
            uint32 numbers[3] = {};
            std::istringstream in(className);
            for (uint32& number : numbers)
                if (!(in >> number))
                    break;

            // Raids built by players, by leader: the group and when it was built.
            static std::unordered_map<ObjectGuid, std::pair<ObjectGuid, time_t>> playerRaids;
            time_t const now = time(nullptr);
            if (!gameMaster)
            {
                uint32 const cooldown = sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaRaidPlayerCooldown", 30) * MINUTE;
                uint32 const maxRaids = sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaRaidPlayerMax", 5);

                uint32 active = 0;
                for (auto it = playerRaids.begin(); it != playerRaids.end();)
                {
                    Group* group = sGroupMgr->GetGroupByGUID(it->second.first.GetCounter());
                    bool withBots = false;
                    if (group)
                        for (GroupReference* ref = group->GetFirstMember(); ref && !withBots; ref = ref->next())
                            withBots = ref->GetSource() && GET_PLAYERBOT_AI(ref->GetSource());
                    if (!withBots && now - it->second.second >= time_t(cooldown))
                    {
                        it = playerRaids.erase(it);
                        continue;
                    }
                    if (withBots)
                        ++active;
                    ++it;
                }

                auto const mine = playerRaids.find(master->GetGUID());
                if (mine != playerRaids.end() && now - mine->second.second < time_t(cooldown))
                {
                    uint32 const minutes = uint32((cooldown - (now - mine->second.second) + MINUTE - 1) / MINUTE);
                    handler->SendSysMessage("You can build another raid in " + std::to_string(minutes) + " minutes.");
                    return true;
                }
                if (active >= maxRaids)
                {
                    handler->SendSysMessage("The realm already has " + std::to_string(active) +
                                            " raids of bots; try again when one of them is over.");
                    return true;
                }
            }

            std::string message;
            bool const built = RecruitCoaRaid(master, numbers[0], numbers[1], numbers[2], message);
            if (built && !gameMaster && master->GetGroup())
                playerRaids[master->GetGUID()] = { master->GetGroup()->GetGUID(), now };
            handler->SendSysMessage(message);
            return true;
        }

        if (wanted == "regear" && gameMaster)
        {
            uint32 const minLevel = className.empty() ? 60 : std::max(1, std::atoi(className.c_str()));
            uint32 done = 0, skipped = 0;
            for (auto const& [guid, bot] : sRandomPlayerbotMgr.GetAllBots())
            {
                if (!bot || !bot->IsInWorld() || bot->GetLevel() < minLevel)
                    continue;
                if (bot->IsInCombat() || bot->InBattleground() || bot->InArena() || bot->IsBeingTeleported())
                {
                    ++skipped;
                    continue;
                }
                // Enchants too: bots geared while enchants were skipped kept bare gear (17/10, half a damage dealer's power).
                PlayerbotFactory factory(bot, bot->GetLevel());
                factory.InitEquipment(false);
                factory.ApplyEnchantAndGemsNew();
                ++done;
            }
            handler->SendSysMessage(std::to_string(done) + " bots of level " + std::to_string(minLevel) +
                                    "+ geared again, " + std::to_string(skipped) + " left for later (in combat or in a battleground).");
            return true;
        }

        if (wanted == "wcr" && gameMaster)
        {
            HandleCoaWcr(handler, className);
            return true;
        }

        if (!gameMaster)
        {
            handler->SendSysMessage("Usage: .playerbots coa raid [size] [tanks] [healers], e.g. .playerbots coa raid 25. "
                                    "To find a single tank or healer, say \"lfg bot tank\" or \"lfg bot heal\" in a chat channel.");
            return true;
        }

        CoaRole role;
        if (wanted == "tank")
            role = CoaRole::Tank;
        else if (wanted == "heal" || wanted == "healer")
            role = CoaRole::Heal;
        else if (wanted == "dps")
            role = CoaRole::Dps;
        else
        {
            handler->SendSysMessage("Usage: .playerbots coa tank|heal|dps [class], e.g. .playerbots coa heal sun cleric; "
                                    ".playerbots coa raid [size] [tanks] [healers], e.g. .playerbots coa raid 25");
            return true;
        }

        uint8 classId = 0;
        if (!className.empty())
        {
            classId = FindCoaClass(className);
            if (!classId)
            {
                handler->SendSysMessage("Unknown or ambiguous class '" + className + "'. Classes: Barbarian, Witch Doctor, "
                                        "Felsworn, Witch Hunter, Stormbringer, Knight of Xoroth, Guardian, Templar, "
                                        "Bloodmage, Ranger, Chronomancer, Necromancer, Pyromancer, Cultist, Starcaller, "
                                        "Sun Cleric, Tinker, Venomancer, Reaper, Primalist, Runemaster.");
                return true;
            }
        }

        if (!master)
            return false;

        // Handled either way: returning false would add the generic usage text to our message.
        std::string message;
        RecruitCoaBot(master, role, message, classId);
        handler->SendSysMessage(message);
        return true;
    }

    static void HandleCoaWcr(ChatHandler* handler, std::string const& args)
    {
        if (!IsRebornRealm())
        {
            handler->SendSysMessage("not a Warcraft Reborn realm");
            return;
        }
        std::istringstream in(args);
        std::string word, number;
        in >> word >> number;
        if (word == "reload")
        {
            handler->PSendSysMessage("playerbots_coa_wcr_builds: {} guide rows loaded.", LoadWcrBuilds());
            return;
        }

        Player* bot = handler->getSelectedPlayer();
        if (!bot || !GET_PLAYERBOT_AI(bot))
        {
            handler->SendSysMessage("Select a bot: .playerbots coa wcr [spec <0-2>|reload]");
            return;
        }
        if (!IsRebornBot(bot))
        {
            handler->PSendSysMessage("{}: not a Warcraft Reborn class.", bot->GetName());
            return;
        }

        if (word == "spec")
        {
            if (number.size() != 1 || number[0] < '0' || number[0] > '2')
            {
                handler->SendSysMessage("Usage: .playerbots coa wcr spec <0-2>");
                return;
            }
            CoaRole const before = GetWcrRole(bot);
            std::string error;
            if (!SetWcrSpec(bot, uint8(number[0] - '0'), error))
                handler->PSendSysMessage("{}: {}", bot->GetName(), error);
            if (GetWcrRole(bot) != before && sRandomPlayerbotMgr.IsRandomBot(bot))
                PlayerbotFactory(bot, bot->GetLevel()).InitEquipment(true);
            if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
                botAI->ResetStrategies();
        }

        for (std::string const& line : DescribeWcr(bot))
            handler->PSendSysMessage("{}: {}", bot->GetName(), line);
    }

    // .playerbots wildcard redraft [name] : the named random Wildcard Hero bot, or the selected one, forgets
    // its build and drafts it again for its role.
    static bool HandleWildcardRedraftCommand(ChatHandler* handler, Optional<PlayerIdentifier> target)
    {
        if (!target)
            target = PlayerIdentifier::FromTarget(handler);
        Player* bot = target ? target->GetConnectedPlayer() : nullptr;
        if (!bot)
        {
            handler->SendSysMessage("Name an online Wildcard Hero bot or select one: .playerbots wildcard redraft [name]");
            return true;
        }
        if (!RedraftWildcardBot(bot))
        {
            handler->PSendSysMessage("{} is not a random Wildcard Hero bot.", bot->GetName());
            return true;
        }

        if (bot->IsInCombat())
        {
            handler->PSendSysMessage("{} drafted its build again and takes its stat path after the fight.", bot->GetName());
            return true;
        }

        CoaRole const role = GetWildcardRole(bot);
        handler->PSendSysMessage("{} drafted its build again as a {}.", bot->GetName(),
                                 role == CoaRole::Tank ? "tank" : role == CoaRole::Heal ? "healer" : "damage dealer");
        return true;
    }

    static bool HandleRandomPlayerbotCommand(ChatHandler* handler, char const* args)
    {
        return RandomPlayerbotMgr::HandlePlayerbotConsoleCommand(handler, args);
    }

    static bool HandleGuildTaskCommand(ChatHandler* handler, char const* args)
    {
        return GuildTaskMgr::HandleConsoleCommand(handler, args);
    }

    static bool HandlePerfMonCommand(ChatHandler* /*handler*/, char const* args)
    {
        if (!strcmp(args, "reset"))
        {
            sPerfMonitor.Reset();
            return true;
        }

        if (!strcmp(args, "tick"))
        {
            sPerfMonitor.PrintStats(true, false);
            return true;
        }

        if (!strcmp(args, "stack"))
        {
            sPerfMonitor.PrintStats(false, true);
            return true;
        }

        if (!strcmp(args, "toggle"))
        {
            sPlayerbotAIConfig.perfMonEnabled = !sPlayerbotAIConfig.perfMonEnabled;
            if (sPlayerbotAIConfig.perfMonEnabled)
                LOG_INFO("playerbots", "Performance monitor enabled");
            else
                LOG_INFO("playerbots", "Performance monitor disabled");
            return true;
        }

        sPerfMonitor.PrintStats();
        return true;
    }

    // .playerbots missiontest [count] [minutes] [mining|herbalism|skinning|fishing|cloth|all] | status | stop : a test
    // bench of the gathering missions, for random bots with no owner (nothing is mailed, each end is logged as MISSION BENCH
    // END in the CoA bot log). Bots without the profession are taught it for the test, with a skill matching their
    // level, and given its tool.
    static bool HandleMissionTestCommand(ChatHandler* handler, char const* args)
    {
        std::istringstream in(args ? args : "");
        std::string first;
        in >> first;

        std::vector<Player*> bots;
        for (auto const& [guid, bot] : sRandomPlayerbotMgr.GetAllBots())
            if (bot && bot->IsInWorld())
                bots.push_back(bot);

        if (first == "status" || first == "stop")
        {
            uint32 count = 0;
            for (Player* bot : bots)
            {
                if (!sCoaMissionMgr.IsBench(bot))
                    continue;
                ++count;
                if (first == "stop")
                    sCoaMissionMgr.Finish(bot, "bench stopped");
                else
                    handler->PSendSysMessage("{} lvl {}: {}", bot->GetName(), bot->GetLevel(),
                                             sCoaMissionMgr.StatusLine(bot));
            }
            handler->PSendSysMessage("{} bench missions {}.", count, first == "stop" ? "stopped" : "running");
            return true;
        }

        // The numbers are the count then the minutes. Without a profession, half mine and half pick herbs; "all"
        // takes the four in turn.
        uint32 wanted = 10;
        uint32 minutes = 60;
        std::vector<uint32> professions = {SKILL_MINING, SKILL_HERBALISM};
        uint32 numbers = 0;
        for (std::string word = first; !word.empty(); word.clear(), in >> word)
        {
            if (word == "mining")
                professions = {SKILL_MINING};
            else if (word == "herbalism")
                professions = {SKILL_HERBALISM};
            else if (word == "skinning")
                professions = {SKILL_SKINNING};
            else if (word == "fishing")
                professions = {SKILL_FISHING};
            else if (word == "cloth")
                professions = {TravelMgr::GATHER_CLOTH};
            else if (word == "all")
                professions = {SKILL_MINING, SKILL_HERBALISM, SKILL_SKINNING, SKILL_FISHING};
            else if (numbers++ == 0)
                wanted = std::max<uint32>(1, uint32(std::atoi(word.c_str())));
            else
                minutes = std::max<uint32>(1, uint32(std::atoi(word.c_str())));
        }

        Acore::Containers::RandomShuffle(bots);
        uint32 started = 0;
        uint32 taught = 0;
        for (Player* bot : bots)
        {
            if (started >= wanted)
                break;
            PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
            if (!botAI || bot->GetLevel() < 5 || !bot->IsAlive() || bot->IsInCombat() || bot->GetGroup() ||
                sCoaMissionMgr.IsOnMission(bot))
                continue;

            // The professions in turn; a bot that does not know it is taught it for the test, and given its tool.
            uint32 const skillId = professions[started % professions.size()];
            if (skillId != TravelMgr::GATHER_CLOTH && !bot->HasSkill(skillId))
            {
                uint32 const value = std::min<uint32>(225, bot->GetLevel() * 5);
                uint32 const step = value > 150 ? 3 : value > 75 ? 2 : 1;
                uint32 spells[3] = {2575u, 2576u, 3564u};
                uint32 tool = 2901;
                if (skillId == SKILL_HERBALISM)
                {
                    spells[0] = 2366u, spells[1] = 2368u, spells[2] = 3570u;
                    tool = 0;
                }
                else if (skillId == SKILL_SKINNING)
                {
                    spells[0] = 8613u, spells[1] = 8617u, spells[2] = 8618u;
                    tool = 7005;
                }
                else if (skillId == SKILL_FISHING)
                {
                    spells[0] = 7620u, spells[1] = 7731u, spells[2] = 7732u;
                    tool = 6256;
                }
                for (uint32 i = 0; i < step; ++i)
                    bot->learnSpell(spells[i], false);
                bot->SetSkill(skillId, step, value, step * 75);
                if (tool && !bot->HasItemCount(tool, 1))
                    bot->AddItem(tool, 1);
                ++taught;
            }

            CoaMissionAction action(botAI);
            std::string error;
            // One bot in three takes an order instead: 10 Copper Ore, Peacebloom, Light Leather or Raw Brilliant
            // Smallfish or Linen Cloth.
            uint32 const orders[5] = {2770u, 2447u, 2318u, 6291u, 2589u};
            uint32 const kind = skillId == SKILL_MINING      ? 0
                                : skillId == SKILL_HERBALISM ? 1
                                : skillId == SKILL_SKINNING  ? 2
                                : skillId == SKILL_FISHING   ? 3
                                                             : 4;
            uint32 const order = (started % 3 == 2) ? orders[kind] : 0u;
            if (!action.Start(nullptr, minutes, order ? 0 : skillId, error, order, order ? 10 : 0))
            {
                handler->PSendSysMessage("{} lvl {}: {}", bot->GetName(), bot->GetLevel(), error);
                continue;
            }
            ++started;
        }
        handler->PSendSysMessage("{} bench missions started for {} min ({} bots taught a profession).", started,
                                 minutes, taught);
        return true;
    }

    static bool HandleDebugBGCommand(ChatHandler* handler, char const* args)
    {
        return BGTactics::HandleConsoleCommand(handler, args);
    }

    static bool HandleSetSecurityKeyCommand(ChatHandler* handler, char const* args)
    {
        if (!args || !*args)
        {
            handler->PSendSysMessage("Usage: .playerbots account setKey <securityKey>");
            return false;
        }

        Player* player = handler->GetSession()->GetPlayer();
        std::string key = args;

        PlayerbotMgr* mgr = PlayerbotsMgr::instance().GetPlayerbotMgr(player);
        if (mgr)
        {
            mgr->HandleSetSecurityKeyCommand(player, key);
            return true;
        }
        else
        {
            handler->PSendSysMessage("PlayerbotMgr instance not found.");
            return false;
        }
    }

    static bool HandleLinkAccountCommand(ChatHandler* handler, char const* args)
    {
        if (!args || !*args)
            return false;

        char* accountName = strtok((char*)args, " ");
        char* key = strtok(nullptr, " ");

        if (!accountName || !key)
        {
            handler->PSendSysMessage("Usage: .playerbots account link <accountName> <securityKey>");
            return false;
        }

        Player* player = handler->GetSession()->GetPlayer();

        PlayerbotMgr* mgr = PlayerbotsMgr::instance().GetPlayerbotMgr(player);
        if (mgr)
        {
            mgr->HandleLinkAccountCommand(player, accountName, key);
            return true;
        }
        else
        {
            handler->PSendSysMessage("PlayerbotMgr instance not found.");
            return false;
        }
    }

    static bool HandleViewLinkedAccountsCommand(ChatHandler* handler, char const* /*args*/)
    {
        Player* player = handler->GetSession()->GetPlayer();

        PlayerbotMgr* mgr = PlayerbotsMgr::instance().GetPlayerbotMgr(player);
        if (mgr)
        {
            mgr->HandleViewLinkedAccountsCommand(player);
            return true;
        }
        else
        {
            handler->PSendSysMessage("PlayerbotMgr instance not found.");
            return false;
        }
    }

    static bool HandleUnlinkAccountCommand(ChatHandler* handler, char const* args)
    {
        if (!args || !*args)
            return false;

        char* accountName = strtok((char*)args, " ");
        if (!accountName)
        {
            handler->PSendSysMessage("Usage: .playerbots account unlink <accountName>");
            return false;
        }

        Player* player = handler->GetSession()->GetPlayer();

        PlayerbotMgr* mgr = PlayerbotsMgr::instance().GetPlayerbotMgr(player);
        if (mgr)
        {
            mgr->HandleUnlinkAccountCommand(player, accountName);
            return true;
        }
        else
        {
            handler->PSendSysMessage("PlayerbotMgr instance not found.");
            return false;
        }
    }
};

void AddPlayerbotsCommandscripts() { new playerbots_commandscript(); }
