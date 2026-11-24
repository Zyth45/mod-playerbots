/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaBotWhy.h"

#include "Chat.h"
#include "ChatCommand.h"
#include "CoaBotWhyRules.h"
#include "CoaMoveAuditRules.h"
#include "Config.h"
#include "Creature.h"
#include "DBCStores.h"
#include "Engine.h"
#include "GameObject.h"
#include "GameTime.h"
#include "Map.h"
#include "MotionMaster.h"
#include "NewRpgInfo.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "PathGenerator.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "WorldSession.h"

#include <cfloat>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <variant>

using namespace Acore::ChatCommands;

namespace CoaBotWhy
{
namespace
{
    bool enabled = true;
    std::string fileName = "squidbots_why.log";
    bool gmOnly = false;          // AiPlayerbot.CoaWhy.GmOnly
    uint32 cooldownMs = 5000;     // AiPlayerbot.CoaWhy.Cooldown (seconds)
    std::unordered_map<uint64, uint64> lastAsked;  // player guid -> when (world thread)

    uint64 NowMs() { return uint64(GameTime::GetGameTimeMS().count()); }

    std::string Num(float value)
    {
        std::ostringstream out;
        out << std::fixed << std::setprecision(1) << value;
        return out.str();
    }

    std::string Pos(uint32 mapId, float x, float y, float z)
    {
        return "map " + std::to_string(mapId) + " " + Num(x) + " " + Num(y) + " " + Num(z);
    }

    std::string ZoneName(uint32 zoneId)
    {
        AreaTableEntry const* zone = sAreaTableStore.LookupEntry(zoneId);
        return zone ? PlayerbotAI::GetLocalizedAreaName(zone) : std::to_string(zoneId);
    }

    // Where its RPG status takes it, and what to: the destination, a name, and whether there is one.
    struct Goal
    {
        std::string status;
        std::string what;
        bool hasPos = false;
        uint32 mapId = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };

    void SetPos(Goal& goal, WorldPosition const& pos)
    {
        if (!pos.GetMapId() && !pos.GetPositionX() && !pos.GetPositionY())
            return;
        goal.hasPos = true;
        goal.mapId = pos.GetMapId();
        goal.x = pos.GetPositionX();
        goal.y = pos.GetPositionY();
        goal.z = pos.GetPositionZ();
    }

    Goal GoalOf(Player* bot, PlayerbotAI* botAI)
    {
        Goal goal;
        NewRpgInfo& info = botAI->rpgInfo;
        std::visit([&](auto&& arg)
        {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, NewRpgInfo::GoGrind>)
            {
                goal.status = "GO_GRIND";
                SetPos(goal, arg.pos);
            }
            else if constexpr (std::is_same_v<T, NewRpgInfo::GoCamp>)
            {
                goal.status = "GO_CAMP";
                SetPos(goal, arg.pos);
            }
            else if constexpr (std::is_same_v<T, NewRpgInfo::GoCity>)
            {
                goal.status = "GO_CITY";
                SetPos(goal, arg.pos);
            }
            else if constexpr (std::is_same_v<T, NewRpgInfo::WanderNpc>)
            {
                goal.status = "WANDER_NPC";
                if (WorldObject* object = arg.npcOrGo ? ObjectAccessor::GetWorldObject(*bot, arg.npcOrGo) : nullptr)
                {
                    goal.what = object->GetName() + " (entry " + std::to_string(object->GetEntry()) + ")";
                    goal.hasPos = true;
                    goal.mapId = object->GetMapId();
                    goal.x = object->GetPositionX();
                    goal.y = object->GetPositionY();
                    goal.z = object->GetPositionZ();
                }
                else if (arg.npcOrGo)
                    goal.what = "an NPC or object out of sight (" + arg.npcOrGo.ToString() + ")";
            }
            else if constexpr (std::is_same_v<T, NewRpgInfo::WanderRandom>)
                goal.status = "WANDER_RANDOM";
            else if constexpr (std::is_same_v<T, NewRpgInfo::DoQuest>)
            {
                goal.status = "DO_QUEST";
                Quest const* quest = sObjectMgr->GetQuestTemplate(arg.questId);
                goal.what = "quest " + std::to_string(arg.questId) + (quest ? " \"" + quest->GetTitle() + "\"" : "") +
                            ", objective " + std::to_string(arg.objectiveIdx);
                SetPos(goal, arg.pos);
            }
            else if constexpr (std::is_same_v<T, NewRpgInfo::TravelFlight>)
            {
                goal.status = arg.inFlight ? "TRAVEL_FLIGHT (in flight)" : "TRAVEL_FLIGHT";
                CreatureTemplate const* master = sObjectMgr->GetCreatureTemplate(arg.flightMasterEntry);
                goal.what = "flight master " + (master ? master->Name : std::to_string(arg.flightMasterEntry));
                if (!arg.inFlight)
                    SetPos(goal, arg.flightMasterPos);
            }
            else if constexpr (std::is_same_v<T, NewRpgInfo::Rest>)
                goal.status = "REST";
            else if constexpr (std::is_same_v<T, NewRpgInfo::OutdoorPvP>)
                goal.status = "OUTDOOR_PVP";
            else
                goal.status = "IDLE";
        }, info.data);
        goal.status += " for " + CoaBotWhyRules::Duration(info.startT ? GetMSTimeDiffToNow(info.startT) : 0);
        // A far move under way (MOVE_FAR) when the status itself has no place.
        if (!goal.hasPos && info.nearestMoveFarDis != FLT_MAX)
            SetPos(goal, info.moveFarPos);
        return goal;
    }

    // The path the core would give it to there now.
    std::string PathTo(Player* bot, Goal const& goal)
    {
        if (!goal.hasPos)
            return "no destination";
        if (goal.mapId != bot->GetMapId())
            return "destination on map " + std::to_string(goal.mapId);
        float const distance = bot->GetDistance(goal.x, goal.y, goal.z);
        PathGenerator path(bot);
        path.CalculatePath(goal.x, goal.y, goal.z, false);
        G3D::Vector3 const end = path.GetActualEndPosition();
        float const gap = std::sqrt((end.x - goal.x) * (end.x - goal.x) + (end.y - goal.y) * (end.y - goal.y) +
                                    (end.z - goal.z) * (end.z - goal.z));
        return Num(distance) + " yd away, path " + CoaMoveAuditRules::PathTypeText(uint32(path.GetPathType())) + ", " +
               std::to_string(path.GetPath().size()) + " points, ends " + Num(gap) + " yd short";
    }

    std::string Stamp()
    {
        time_t const now = GameTime::GetGameTime().count();
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        std::ostringstream out;
        out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return out.str();
    }

    // The file next to the server's logs (LogsDir), as the other logs.
    void WriteLine(std::string const& line)
    {
        std::string path = sConfigMgr->GetOption<std::string>("LogsDir", "");
        if (!path.empty() && path.back() != '/' && path.back() != '\\')
            path += '/';
        std::ofstream file(path + fileName, std::ios::app);
        if (file)
            file << Stamp() << " " << line << '\n';
    }

    bool HandleWhy(ChatHandler* handler, Optional<PlayerIdentifier> target)
    {
        if (!enabled)
        {
            handler->SendSysMessage("[SquidBots why] off (AiPlayerbot.CoaWhy.Enable = 0).");
            return true;
        }
        // Open to every account (a command refused to a player account would be said aloud in /say when the realm
        // has AllowPlayerCommands = 0); GmOnly keeps it to game masters, with an answer, never in silence.
        WorldSession* session = handler->GetSession();
        if (gmOnly && session && session->GetSecurity() < SEC_GAMEMASTER)
        {
            handler->SendSysMessage("[SquidBots why] GM account needed.");
            return true;
        }
        if (session && session->GetPlayer() && cooldownMs)
        {
            uint64& last = lastAsked[session->GetPlayer()->GetGUID().GetRawValue()];
            uint64 const now = NowMs();
            if (last && now < last + cooldownMs)
            {
                handler->PSendSysMessage("[SquidBots why] wait {} s between two questions.",
                                         (last + cooldownMs - now + 999) / 1000);
                return true;
            }
            last = now;
        }
        if (!target)
            target = PlayerIdentifier::FromTarget(handler);
        Player* bot = target ? target->GetConnectedPlayer() : nullptr;
        PlayerbotAI* botAI = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
        if (!bot || !botAI || !botAI->IsBotAI())
        {
            handler->SendSysMessage("[SquidBots why] select a bot, or name one: .squidbots why [name]");
            return true;
        }

        uint64 const now = NowMs();
        Trace const& trace = botAI->coaWhy;
        Goal const goal = GoalOf(bot, botAI);
        Engine* engine = botAI->GetEngine(botAI->GetState());
        Player* master = botAI->GetMaster();

        std::vector<std::string> lines;
        lines.push_back(bot->GetName() + ", level " + std::to_string(bot->GetLevel()) + ", " +
                        (sRandomPlayerbotMgr.IsRandomBot(bot) ? "random bot" : "alt bot") +
                        (master && master != bot ? " of " + master->GetName() : "") + ", " +
                        ZoneName(bot->GetZoneId()) + " (area " + std::to_string(bot->GetAreaId()) + "), " +
                        Pos(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()) +
                        (bot->isDead() ? ", dead" : "") + (bot->IsInCombat() ? ", in combat" : "") +
                        (bot->IsMounted() ? ", mounted" : "") + (bot->IsInFlight() ? ", on a taxi" : ""));
        lines.push_back("RPG " + goal.status + (goal.what.empty() ? "" : ", " + goal.what) +
                        (goal.hasPos ? ", to " + Pos(goal.mapId, goal.x, goal.y, goal.z) : "") + "; " +
                        PathTo(bot, goal) +
                        (botAI->rpgInfo.stuckAttempts ? ", " + std::to_string(botAI->rpgInfo.stuckAttempts) +
                                                            " stuck attempt(s)"
                                                      : ""));
        lines.push_back(
            "Last move: " +
            (trace.moveAtMs ? trace.moveAction + " to " + Pos(trace.moveMapId, trace.moveX, trace.moveY, trace.moveZ) +
                                  ", " + CoaBotWhyRules::Duration(now - trace.moveAtMs) + " ago"
                            : std::string("none seen")) +
            "; last action: " + (engine ? engine->GetLastAction() : std::string("none")) + "; motion " +
            std::to_string(uint32(bot->GetMotionMaster()->GetCurrentMovementGeneratorType())) +
            "; still for " + (trace.stillSinceMs ? CoaBotWhyRules::Duration(now - trace.stillSinceMs) : "?"));

        std::string joined;
        for (std::string const& line : lines)
        {
            handler->PSendSysMessage("[SquidBots why] {}", line);
            joined += (joined.empty() ? "" : " | ") + line;
        }
        WriteLine(joined);
        handler->PSendSysMessage("[SquidBots why] written to {} in the server's logs folder.", fileName);
        return true;
    }

    class CoaBotWhyCommandScript : public CommandScript
    {
    public:
        CoaBotWhyCommandScript() : CommandScript("CoaBotWhyCommandScript") {}

        ChatCommandTable GetCommands() const override
        {
            static ChatCommandTable squidbotsTable = {
                {"why", HandleWhy, SEC_PLAYER, Console::No},
            };
            static ChatCommandTable commandTable = {
                {"squidbots", squidbotsTable},
            };
            return commandTable;
        }
    };

    class CoaBotWhyWorldScript : public WorldScript
    {
    public:
        CoaBotWhyWorldScript() : WorldScript("CoaBotWhyWorldScript", {WORLDHOOK_ON_AFTER_CONFIG_LOAD}) {}

        void OnAfterConfigLoad(bool /*reload*/) override
        {
            enabled = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaWhy.Enable", true, false);
            fileName = sConfigMgr->GetOption<std::string>("AiPlayerbot.CoaWhy.File", "squidbots_why.log", false);
            gmOnly = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaWhy.GmOnly", false, false);
            cooldownMs = 1000 * sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaWhy.Cooldown", 5, false);
        }
    };
}  // namespace

bool Enabled() { return enabled; }

void Tick(PlayerbotAI* botAI)
{
    if (!enabled || !botAI)
        return;
    Player* bot = botAI->GetBot();
    if (!bot || !bot->IsInWorld())
        return;
    Trace& trace = botAI->coaWhy;
    float const x = bot->GetPositionX(), y = bot->GetPositionY(), z = bot->GetPositionZ();
    if (!trace.stillSinceMs || trace.stillMapId != bot->GetMapId() ||
        CoaBotWhyRules::Moved(x - trace.stillX, y - trace.stillY, z - trace.stillZ))
    {
        trace.stillX = x;
        trace.stillY = y;
        trace.stillZ = z;
        trace.stillMapId = bot->GetMapId();
        trace.stillSinceMs = NowMs();
    }
}

void NoteMove(PlayerbotAI* botAI, std::string const& action, float x, float y, float z)
{
    if (!enabled || !botAI || !botAI->GetBot())
        return;
    Trace& trace = botAI->coaWhy;
    if (trace.moveAction != action)
        trace.moveAction = action;
    trace.moveX = x;
    trace.moveY = y;
    trace.moveZ = z;
    trace.moveMapId = botAI->GetBot()->GetMapId();
    trace.moveAtMs = NowMs();
}
}  // namespace CoaBotWhy

void AddSC_coa_bot_why()
{
    new CoaBotWhy::CoaBotWhyCommandScript();
    new CoaBotWhy::CoaBotWhyWorldScript();
}
