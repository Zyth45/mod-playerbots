/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaQuietAchievements.h"

#include "Config.h"
#include "GameTime.h"
#include "Opcodes.h"
#include "Player.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "WorldPacket.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace
{
std::shared_mutex quietLock;
std::unordered_map<uint64_t, uint32_t> quiet;  // bot guid -> nested scopes (map threads)
std::atomic<uint32_t> quietCount{0};           // the fast way out of every packet sent
// 3.2: a random bot's first minutes online, bot guid -> until when (game time, ms); same lock.
std::unordered_map<uint64_t, uint64_t> loginWindows;
std::atomic<uint32_t> windowCount{0};
uint64_t loginWindowMs = 3 * 60 * 1000;  // AiPlayerbot.Coa.HideLoginAchievementsMinutes
// 3.2: one announcement per random bot per gap. The decision is taken before the core announces the achievement
// (OnPlayerBeforeAchievementComplete) and holds for the packets of that announcement (to each guild member and
// player around), until OnPlayerAchievementComplete or MUTE_HOLD_MS. Same lock.
uint64_t announceGapMs = 60 * 1000;  // AiPlayerbot.Coa.AchievementAnnounceGapSeconds
constexpr uint64_t MUTE_HOLD_MS = 2000;
uint64_t realmGapMs = 60 * 1000;  // AiPlayerbot.Coa.AchievementAnnounceRealmGapSeconds
uint64_t realmLastAnnounce = 0;   // when a random bot's achievement was last announced in the realm; same lock
std::unordered_map<uint64_t, uint64_t> lastAnnounce;  // bot guid -> when its last achievement was announced
std::unordered_map<uint64_t, uint64_t> muted;         // bot guid -> its announcement under way is dropped until then
std::atomic<uint32_t> mutedCount{0};

uint64_t NowMs() { return uint64_t(GameTime::GetGameTimeMS().count()); }

bool IsQuiet(uint64_t guid)
{
    std::shared_lock<std::shared_mutex> guard(quietLock);
    if (quiet.count(guid))
        return true;
    uint64_t const now = NowMs();
    auto it = loginWindows.find(guid);
    if (it != loginWindows.end() && CoaQuietAchievements::InLoginWindow(now, it->second))
        return true;
    auto mute = muted.find(guid);
    return mute != muted.end() && now < mute->second;
}

// Before the core announces an achievement of a random bot: dropped when another of this bot was announced within
// the gap, or another of any random bot within the realm gap.
void DecideAnnounce(Player* player)
{
    if ((!announceGapMs && !realmGapMs) || !player || !sRandomPlayerbotMgr.IsRandomBot(player))
        return;
    uint64_t const guid = player->GetGUID().GetRawValue();
    uint64_t const now = NowMs();
    std::unique_lock<std::shared_mutex> guard(quietLock);
    if (quiet.count(guid))
        return;  // the factory or a simulated credit: silent anyway
    auto window = loginWindows.find(guid);
    if (window != loginWindows.end() && CoaQuietAchievements::InLoginWindow(now, window->second))
        return;
    uint64_t& last = lastAnnounce[guid];
    if (!CoaQuietAchievements::Announces(now, last, realmLastAnnounce, announceGapMs, realmGapMs))
    {
        auto [it, fresh] = muted.try_emplace(guid, now + MUTE_HOLD_MS);
        if (fresh)
            mutedCount.fetch_add(1, std::memory_order_relaxed);
        else
            it->second = now + MUTE_HOLD_MS;
        return;
    }
    last = now;
    realmLastAnnounce = now;
}

void EndAnnounce(Player* player)
{
    if (!player || !mutedCount.load(std::memory_order_relaxed))
        return;
    std::unique_lock<std::shared_mutex> guard(quietLock);
    if (muted.erase(player->GetGUID().GetRawValue()))
        mutedCount.fetch_sub(1, std::memory_order_relaxed);
}

void OpenLoginWindow(Player* player)
{
    if (!loginWindowMs || !player || !sRandomPlayerbotMgr.IsRandomBot(player))
        return;
    uint64_t const until = NowMs() + loginWindowMs;
    std::unique_lock<std::shared_mutex> guard(quietLock);
    auto [it, fresh] = loginWindows.try_emplace(player->GetGUID().GetRawValue(), until);
    if (fresh)
        windowCount.fetch_add(1, std::memory_order_relaxed);
    else
        it->second = std::max(it->second, until);
}

// The windows that ended (world thread, every few seconds).
void CloseEndedWindows()
{
    uint64_t const now = NowMs();
    {
        std::unique_lock<std::shared_mutex> guard(quietLock);
        for (auto it = muted.begin(); it != muted.end();)
            if (now >= it->second)
            {
                it = muted.erase(it);
                mutedCount.fetch_sub(1, std::memory_order_relaxed);
            }
            else
                ++it;
        if (lastAnnounce.size() > 20000)
            for (auto it = lastAnnounce.begin(); it != lastAnnounce.end();)
                it = now >= it->second + announceGapMs ? lastAnnounce.erase(it) : std::next(it);
    }
    if (!windowCount.load(std::memory_order_relaxed))
        return;
    std::unique_lock<std::shared_mutex> guard(quietLock);
    for (auto it = loginWindows.begin(); it != loginWindows.end();)
    {
        if (!CoaQuietAchievements::InLoginWindow(now, it->second))
        {
            it = loginWindows.erase(it);
            windowCount.fetch_sub(1, std::memory_order_relaxed);
        }
        else
            ++it;
    }
}

class CoaQuietAchievementsServerScript : public ServerScript
{
public:
    CoaQuietAchievementsServerScript() : ServerScript("CoaQuietAchievementsServerScript", {SERVERHOOK_CAN_PACKET_SEND}) {}

    // Every packet of the realm passes here: out at once unless a bot is being made right now.
    bool CanPacketSend(WorldSession* /*session*/, WorldPacket const& packet) override
    {
        if (!quietCount.load(std::memory_order_relaxed) && !windowCount.load(std::memory_order_relaxed) &&
            !mutedCount.load(std::memory_order_relaxed))
            return true;
        uint16 const opcode = packet.GetOpcode();
        if (opcode != SMSG_MESSAGECHAT && opcode != SMSG_SERVER_FIRST_ACHIEVEMENT)
            return true;
        uint64_t sender = 0;
        bool const announce =
            opcode == SMSG_MESSAGECHAT
                ? CoaQuietAchievements::AchievementChatSender(packet.contents(), packet.size(), sender)
                : CoaQuietAchievements::ServerFirstSender(packet.contents(), packet.size(), sender);
        return !(announce && IsQuiet(sender));
    }
};
// 3.2: the window opens when the bot is loaded (before the level-up of its login) and again at its login.
class CoaLoginAchievementsPlayerScript : public PlayerScript
{
public:
    CoaLoginAchievementsPlayerScript()
        : PlayerScript("CoaLoginAchievementsPlayerScript", {PLAYERHOOK_ON_LOAD_FROM_DB, PLAYERHOOK_ON_LOGIN,
                                                            PLAYERHOOK_ON_BEFORE_ACHI_COMPLETE, PLAYERHOOK_ON_ACHI_COMPLETE}) {}

    void OnPlayerLoadFromDB(Player* player) override { OpenLoginWindow(player); }
    void OnPlayerLogin(Player* player) override { OpenLoginWindow(player); }
    // 3.2: one announcement per bot per gap (the achievement itself is never refused here).
    bool OnPlayerBeforeAchievementComplete(Player* player, AchievementEntry const* /*achievement*/) override
    {
        DecideAnnounce(player);
        return true;
    }
    void OnPlayerAchievementComplete(Player* player, AchievementEntry const* /*achievement*/) override
    {
        EndAnnounce(player);
    }
};

class CoaLoginAchievementsWorldScript : public WorldScript
{
public:
    CoaLoginAchievementsWorldScript()
        : WorldScript("CoaLoginAchievementsWorldScript", {WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_UPDATE}) {}

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        loginWindowMs = uint64_t(60 * 1000) *
                        sConfigMgr->GetOption<uint32>("AiPlayerbot.Coa.HideLoginAchievementsMinutes", 3, false);
        announceGapMs = uint64_t(1000) *
                        sConfigMgr->GetOption<uint32>("AiPlayerbot.Coa.AchievementAnnounceGapSeconds", 60, false);
        realmGapMs = uint64_t(1000) *
                     sConfigMgr->GetOption<uint32>("AiPlayerbot.Coa.AchievementAnnounceRealmGapSeconds", 60, false);
    }

    void OnUpdate(uint32 diff) override
    {
        sweepTimer += diff;
        if (sweepTimer < 5000)
            return;
        sweepTimer = 0;
        CloseEndedWindows();
    }

private:
    uint32 sweepTimer = 0;
};
}  // namespace

namespace CoaQuietAchievements
{
Scope::Scope(Player* bot)
{
    if (!bot)
        return;
    guid = bot->GetGUID().GetRawValue();
    std::unique_lock<std::shared_mutex> guard(quietLock);
    if (quiet[guid]++ == 0)
        ++quietCount;
}

Scope::~Scope()
{
    if (!guid)
        return;
    std::unique_lock<std::shared_mutex> guard(quietLock);
    auto it = quiet.find(guid);
    if (it != quiet.end() && --it->second == 0)
    {
        quiet.erase(it);
        --quietCount;
    }
}
}  // namespace CoaQuietAchievements

void AddSC_coa_quiet_achievements()
{
    new CoaQuietAchievementsServerScript();
    new CoaLoginAchievementsPlayerScript();
    new CoaLoginAchievementsWorldScript();
}
