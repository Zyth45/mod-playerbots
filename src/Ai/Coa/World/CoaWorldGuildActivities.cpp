/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

/*
 * CoA World: guild activities for a real player, from the SquidBots Lite addon (AiPlayerbot.CoaWorld.Guilds.Activities,
 * SQBW GACT? / GACT <kind>, WORLD-PROTOCOLE-3.0 1b).
 *  - gather: 3 to 8 free bots of the player's guild come to the player and stay GatherMinutes, then go back to their
 *    lives. The near ones walk; GatherBringSeconds after the start, those still far (another map, or too far to come
 *    soon) land 40 to 60 m behind the player, out of sight, on ground the player could walk to, and come on foot or
 *    mounted. Never into a dungeon or a battleground; a bot in combat waits for its end;
 *  - party5: a party of 5 for a dungeon, one tank, one healer, damage dealers, by the bots' recruiter (RecruitCoaBot);
 *  - raid10 / raid15 / raid25: a raid built as ".playerbots coa raid" (RecruitCoaRaid, its roles and its flags).
 *  For both, the bots of the player's guild first, then random bots to complete, none taken from a dungeon, raid or
 *  battleground (SquidBot's onlyGuildId / preferGuildId, bb468166).
 * One activity at a time per player, then ActivityCooldownMinutes before the next. World thread.
 */

#include "CoaWorldGuildActivities.h"

#include "CoaSpecialization.h"
#include "CoaWorldAddonRules.h"
#include "CoaWorldBots.h"
#include "CoaWorldGuilds.h"

#include "Config.h"
#include "Group.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include "WorldSession.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace
{
using namespace CoaWorld;

constexpr uint32 BOT_OWNER_GATHER_BASE = 700;  // + slot
constexpr uint32 GATHER_SLOTS = 50;

struct Settings
{
    bool enabled = false;  // Guilds.Enable and Guilds.Activities
    bool addon = false;    // Guilds.Addon
    uint32 cooldownMinutes = 10;
    uint32 gatherMinutes = 15;
    uint32 bringSeconds = 30;  // 0: the far ones never come
};
Settings settings;

struct Activity
{
    std::string kind;    // running kind, empty when none
    uint32 slot = 0;     // gather: its borrowing slot
    time_t until = 0;    // gather: its end
    time_t bringAt = 0;  // gather: the far ones are brought from then on (0: done)
    time_t cooldown = 0;  // no new activity before
};
std::unordered_map<uint32, Activity> g_players;  // real player (low guid)
bool g_slotUsed[GATHER_SLOTS] = {};
uint32 t_guild = 0;  // the guild BorrowBots' filter keeps (world thread only)

char const* const KINDS[] = { "gather", "party5", "raid10", "raid15", "raid25" };

bool IsHuman(Player* player)
{
    return player && player->GetSession() && !player->GetSession()->IsBot() && !GET_PLAYERBOT_AI(player) && !CoaWorld::IsTestDriver(player);
}

uint32 FreeGuildBots(Player* player)
{
    uint32 n = 0;
    for (auto const& [guid, bot] : RandomBots())
        if (bot && bot->GetGuildId() == player->GetGuildId() && IsFreeBot(bot))
            ++n;
    return n;
}

std::vector<std::string> Lines(Player* player, time_t now)
{
    Activity const& a = g_players[player->GetGUID().GetCounter()];
    uint32 const free = FreeGuildBots(player);
    std::vector<std::string> out;
    for (char const* kind : KINDS)
    {
        char const* state;
        time_t until = 0;
        if (a.kind == kind)
            state = "busy", until = a.until;
        else if (!a.kind.empty())
            state = "busy";
        else if (a.cooldown > now)
            state = "cooldown", until = a.cooldown;
        else if (std::string(kind) == "gather" && !free)
            state = "nobots";
        else
            state = "ready";
        out.push_back(Acore::StringFormat("GA\t{}\t{}\t{}\t{}", kind, state, free, int64(until)));
    }
    return out;
}

void Push(Player* player)
{
    if (settings.addon && player && player->IsInWorld())
        CoaWorldAddonSendLines(player, Lines(player, time(nullptr)));
}

void EndGather(uint32 low, Activity& a, time_t now)
{
    ReleaseBots(BOT_OWNER_GATHER_BASE + a.slot, true);
    g_slotUsed[a.slot] = false;
    a.kind.clear();
    a.until = 0;
    a.bringAt = 0;
    a.cooldown = now + settings.cooldownMinutes * MINUTE;
    Push(ObjectAccessor::FindConnectedPlayer(ObjectGuid::Create<HighGuid::Player>(low)));
}

constexpr float BRING_NEAR = 120.0f;  // closer: it arrives soon enough on foot

// 40 to 60 m behind the player, on ground the player could walk to; out of the player's line of sight if one is found.
bool LandingSpot(Player* player, Position& out)
{
    Map* map = player->GetMap();
    bool found = false;
    for (int i = 0; i < 8; ++i)
    {
        float const angle = player->GetOrientation() + float(M_PI) + frand(-1.0f, 1.0f);
        float const d = frand(40.0f, 60.0f);
        float x = player->GetPositionX() + d * std::cos(angle);
        float y = player->GetPositionY() + d * std::sin(angle);
        float z = player->GetPositionZ();
        if (!map->CanReachPositionAndGetValidCoords(player, x, y, z) || player->GetExactDist2d(x, y) < 35.0f)
            continue;
        bool const hidden = !player->IsWithinLOS(x, y, z + 2.0f);
        if (!found || hidden)
        {
            out.Relocate(x, y, z);
            found = true;
        }
        if (hidden)
            break;
    }
    return found;
}

// The gathering's bots still far are brought near the player; those that cannot come now are tried again later.
void BringFar(Player* player, Activity& a)
{
    if (!player->IsInWorld() || !player->GetMap() || player->GetMap()->Instanceable() || player->InBattleground() ||
        player->IsInFlight() || player->IsBeingTeleported() || player->IsInCombat())
        return;
    uint32 const owner = BOT_OWNER_GATHER_BASE + a.slot;
    uint32 brought = 0;
    bool pending = false;
    for (ObjectGuid guid : BorrowedBots(owner))
    {
        Player* bot = ObjectAccessor::FindPlayer(guid);
        if (!bot || (bot->GetMapId() == player->GetMapId() && bot->GetExactDist(player) <= BRING_NEAR))
            continue;
        Position at;
        if (LandingSpot(player, at) && BringBot(owner, guid, player->GetMapId(), at, player->GetPosition()))
            ++brought;
        else
            pending = true;
    }
    if (!pending)
        a.bringAt = 0;
    if (brought)
        LOG_DEBUG("module", "CoA World: {} gathering, {} guildmates brought near", player->GetName(), brought);
}

// The party of 5: the group's members count; one tank, one healer, the rest damage dealers.
bool Party5(Player* player, std::string& reason)
{
    Group* group = player->GetGroup();
    if (group && (group->isRaidGroup() || group->isLFGGroup() || group->isBGGroup() || !group->IsLeader(player->GetGUID())))
    {
        reason = "ingroup";
        return false;
    }
    uint32 members = group ? group->GetMembersCount() : 1;
    bool tank = false, heal = false;
    if (group)
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            if (Player* m = ref->GetSource())
            {
                tank |= PlayerbotAI::IsTank(m, true);
                heal |= PlayerbotAI::IsHeal(m, true);
            }
    std::string message;
    bool any = false;
    while (members < 5)
    {
        CoaRole const role = !tank ? CoaRole::Tank : !heal ? CoaRole::Heal : CoaRole::Dps;
        // A bot of the guild first, then any random bot; never one standing in a dungeon, raid or battleground.
        if (!RecruitCoaBot(player, role, message, 0, player->GetGuildId(), true) &&
            !RecruitCoaBot(player, role, message, 0, 0, true))
            break;
        any = true;
        ++members;
        tank |= role == CoaRole::Tank;
        heal |= role == CoaRole::Heal;
    }
    if (!any)
        reason = "nobots";
    return any;
}

class CoaWorldGuildActivitiesScript : public WorldScript
{
public:
    CoaWorldGuildActivitiesScript()
        : WorldScript("CoaWorldGuildActivitiesScript", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE })
    {
    }
    void OnAfterConfigLoad(bool /*reload*/) override { Read(); }
    void OnStartup() override { Read(); }

    void OnUpdate(uint32 diff) override
    {
        if (!settings.enabled)
            return;
        _timer += diff;
        if (_timer < 5 * IN_MILLISECONDS)
            return;
        _timer = 0;
        time_t const now = time(nullptr);
        for (auto& [low, a] : g_players)
        {
            if (a.kind != "gather")
                continue;
            Player* player = ObjectAccessor::FindConnectedPlayer(ObjectGuid::Create<HighGuid::Player>(low));
            if (!player || now >= a.until)
                EndGather(low, a, now);
            else if (a.bringAt && now >= a.bringAt)
                BringFar(player, a);
        }
    }

private:
    static void Read()
    {
        auto key = [](char const* name) { return std::string("AiPlayerbot.CoaWorld.Guilds.") + name; };
        settings.enabled = sConfigMgr->GetOption<bool>(key("Enable"), true, false) &&
                           sConfigMgr->GetOption<bool>(key("Activities"), true, false);
        settings.addon = sConfigMgr->GetOption<bool>(key("Addon"), true, false);
        settings.cooldownMinutes = std::min<uint32>(1440, sConfigMgr->GetOption<uint32>(key("ActivityCooldownMinutes"), 10, false));
        settings.gatherMinutes = std::clamp<uint32>(sConfigMgr->GetOption<uint32>(key("GatherMinutes"), 15, false), 1, 120);
        settings.bringSeconds = std::min<uint32>(600, sConfigMgr->GetOption<uint32>(key("GatherBringSeconds"), 30, false));
    }

    uint32 _timer = 0;
};
}  // namespace

bool CoaWorldGuildActivitiesOn() { return settings.enabled && settings.addon; }

std::vector<std::string> CoaWorldGuildActivityList(Player* player)
{
    if (!IsHuman(player))
        return {};
    return Lines(player, time(nullptr));
}

std::vector<std::string> CoaWorldGuildActivityStart(Player* player, std::vector<std::string> const& args)
{
    auto error = [](char const* reason) { return std::vector<std::string>{ std::string("ERR\tGACT\t") + reason }; };
    if (!IsHuman(player) || !player->GetGuildId())
        return error("off");
    std::string const kind = args.size() > 1 ? args[1] : std::string();
    // GACT cancel: the running gathering ends now; its bots go back to their lives (the far ones not brought yet stay
    // where they are), then the usual cooldown. party5 / raidN have nothing running to cancel.
    if (kind == "cancel")
    {
        uint32 const low = player->GetGUID().GetCounter();
        Activity& running = g_players[low];
        if (running.kind != "gather")
            return error("none");
        time_t const now = time(nullptr);
        EndGather(low, running, now);
        LOG_INFO("module", "CoA World: {} cancelled a gathering", player->GetName());
        return Lines(player, now);
    }
    if (std::find_if(std::begin(KINDS), std::end(KINDS), [&](char const* k) { return kind == k; }) == std::end(KINDS))
        return error("off");
    time_t const now = time(nullptr);
    Activity& a = g_players[player->GetGUID().GetCounter()];
    if (!a.kind.empty())
        return error("busy");
    if (a.cooldown > now)
        return error("cooldown");
    if (player->InBattleground() || player->InArena() || player->IsInCombat())
        return error("busy");
    if (player->GetLevel() < 10)
        return error("level");

    if (kind == "gather")
    {
        uint32 slot = 0;
        while (slot < GATHER_SLOTS && g_slotUsed[slot])
            ++slot;
        if (slot == GATHER_SLOTS)
            return error("busy");
        BorrowRequest request;
        request.mapId = player->GetMapId();
        request.goal = player->GetPosition();
        request.rally = player->GetPosition();
        request.team = uint8(player->GetTeamId());
        request.count = urand(3, 8);
        request.nearRadius = 400.0f;
        request.teleport = false;  // the far ones wait where they are, brought later (BringFar)
        request.holdFar = true;
        t_guild = player->GetGuildId();
        request.filter = [](Player* bot) { return bot->GetGuildId() == t_guild; };
        uint32 const got = BorrowBots(BOT_OWNER_GATHER_BASE + slot, request);
        if (!got)
            return error("nobots");
        g_slotUsed[slot] = true;
        a.kind = kind;
        a.slot = slot;
        a.until = now + settings.gatherMinutes * MINUTE;
        a.bringAt = settings.bringSeconds ? now + settings.bringSeconds : 0;
        LOG_INFO("module", "CoA World: {} gathers {} guildmates", player->GetName(), got);
        return Lines(player, now);
    }

    // party5 / raidN: the bots' recruiter builds the group; nothing to follow afterwards but the cooldown.
    std::string reason;
    bool built;
    if (kind == "party5")
        built = Party5(player, reason);
    else
    {
        std::string message;
        built = RecruitCoaRaid(player, uint32(std::stoul(kind.substr(4))), 0, 0, message, player->GetGuildId());
        if (!built)
            reason = message.find("group") != std::string::npos ? "ingroup" : "nobots";
    }
    if (!built)
        return error(reason.c_str());
    a.cooldown = now + settings.cooldownMinutes * MINUTE;
    LOG_INFO("module", "CoA World: {} built a guild {}", player->GetName(), kind);
    return Lines(player, now);
}

void AddSC_coa_world_guild_activities() { new CoaWorldGuildActivitiesScript(); }
