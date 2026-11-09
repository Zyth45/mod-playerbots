/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

/*
 * CoA World: random bots that got stuck find their way again.
 *
 * Every 15 seconds the positions of the random bots on their way somewhere (going to grind, to a
 * camp, to a quest, to a city) are sampled. Three things count as stuck:
 *  - standing still: within 3 yards for StuckSeconds while it should be travelling;
 *  - going round in circles: within 15 yards of where it was LoopSeconds ago after walking more than
 *    120 yards;
 *  - dying over and over: DeathsLimit deaths within DeathsWindowMinutes;
 *  - back and forth: walking, but no nearer its destination for NoProgressSeconds (a level 60 bot pacing in
 *    Thousand Needles).
 * Out of its level range: a bot far above the zone it walks in (a level 55 in Elwynn, test of 09/11) for
 * OutOfRangeMinutes is sent to a place of its level, out of sight of the players (it waits while one is near,
 * then takes its hearthstone). Capitals are fine, and so is crossing a zone on the way.
 * Bots held by an event (CoA World, the city duels, the level brackets) are left to it.
 * The first time, the bot drops what it was doing and picks something else. When it happens again
 * within ten minutes, or after the deaths, it uses its hearthstone (the real spell, 10 s cast, seen
 * by whoever is near) and rests at home a while.
 *
 * The idea (bounded anomaly detection, then a hearthstone home) comes from Fuitad's
 * mod-playerbots-recovery (GPL-2.0); written again here without its engine hooks, from the world
 * thread only, on what the RPG state and the position say.
 *
 * Config: AiPlayerbot.CoaWorld.Recovery.*, off by default.
 */

#include "CityDuels.h"
#include "CoaWorldBots.h"
#include "CoaWorldState.h"
#include "CoaWorldZoneLevels.h"
#include "RandomBotLevelMgr.h"

#include "Chat.h"
#include "ChatCommand.h"
#include "Config.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "StringFormat.h"

#include <array>
#include <atomic>
#include <mutex>
#include <cmath>
#include <deque>
#include <unordered_map>
#include <unordered_set>

using namespace Acore::ChatCommands;

namespace
{
    using namespace CoaWorld;

    constexpr char const* PREFIX = "AiPlayerbot.CoaWorld.Recovery.";
    constexpr uint32 SAMPLE_MS = 15 * IN_MILLISECONDS;
    constexpr uint32 SPELL_HEARTHSTONE = 8690;
    constexpr time_t RELAPSE_SECONDS = 10 * MINUTE;
    constexpr time_t BUSY_SECONDS = 30;
    constexpr uint32 HEARTH_AT = 3;  // detections within RELAPSE_SECONDS

    struct Settings
    {
        bool enabled = false;
        uint32 stuckSeconds = 90;
        uint32 loopSeconds = 180;
        uint32 deathsLimit = 3;
        uint32 deathsWindowMinutes = 15;
        bool hearth = true;
        uint32 quarantineMinutes = 30;
        uint32 noProgressSeconds = 30;
        uint32 outOfRangeMinutes = 5;
        uint32 outOfRangeLevels = 8;
    };

    Settings settings;

    struct Sample
    {
        time_t when;
        uint32 mapId;
        float x, y, z;
    };

    struct BotTrack
    {
        std::deque<Sample> samples;
        std::deque<time_t> deaths;
        bool wasDead = false;
        time_t lastRecovery = 0;
        time_t lastSeen = 0;
        // Progress towards the destination: the nearest it got, when, and the walk since.
        float destX = 0.0f, destY = 0.0f;
        float bestDist = 0.0f;
        time_t bestAt = 0;
        float walkedSinceBest = 0.0f;
        // Far above the zone it is in: since when, and in which zone.
        uint32 outZone = 0;
        time_t outSince = 0;
        // Busy where it stands (fighting, looting, eating or drinking, casting): not stuck for a while after.
        time_t busyAt = 0;
        // Detections within RELAPSE_SECONDS of each other: the hearthstone only at the third.
        uint32 detections = 0;
    };

    struct Counters
    {
        uint32 stuck = 0;
        uint32 loops = 0;
        uint32 deaths = 0;
        uint32 hearths = 0;
        uint32 noProgress = 0;
        uint32 outOfRange = 0;
    };

    // Quests set aside: (bot, quest) -> until. Written by the world thread, read by the bots' RPG in map threads.
    std::mutex g_quarantineLock;
    std::unordered_map<uint64, time_t> g_quarantine;
    std::atomic<bool> g_anyQuarantine{false};

    uint64 QuarantineKey(uint32 bot, uint32 quest) { return (uint64(bot) << 32) | quest; }

    // The quest the bot was on when it got stuck is left aside for quarantineMinutes.
    void Quarantine(Player* bot, PlayerbotAI* botAI)
    {
        if (!settings.quarantineMinutes)
            return;
        auto const* data = std::get_if<NewRpgInfo::DoQuest>(&botAI->rpgInfo.data);
        if (!data || !data->questId)
            return;
        time_t now = time(nullptr);
        std::lock_guard<std::mutex> lock(g_quarantineLock);
        for (auto itr = g_quarantine.begin(); itr != g_quarantine.end();)
            itr = itr->second <= now ? g_quarantine.erase(itr) : std::next(itr);
        g_quarantine[QuarantineKey(bot->GetGUID().GetCounter(), data->questId)] = now + settings.quarantineMinutes * MINUTE;
        g_anyQuarantine.store(true, std::memory_order_relaxed);
    }

    // RPG states in which a bot is meant to be going somewhere.
    bool IsTravelling(NewRpgStatus status)
    {
        return status == RPG_GO_GRIND || status == RPG_GO_CAMP || status == RPG_DO_QUEST || status == RPG_GO_CITY;
    }

    // Where a travelling bot is going, on its own map; false when the state has no place.
    bool Destination(Player* bot, PlayerbotAI* botAI, float& x, float& y)
    {
        WorldPosition const* pos = nullptr;
        auto const& data = botAI->rpgInfo.data;
        if (auto const* d = std::get_if<NewRpgInfo::GoGrind>(&data))
            pos = &d->pos;
        else if (auto const* d = std::get_if<NewRpgInfo::GoCamp>(&data))
            pos = &d->pos;
        else if (auto const* d = std::get_if<NewRpgInfo::GoCity>(&data))
            pos = &d->pos;
        else if (auto const* d = std::get_if<NewRpgInfo::DoQuest>(&data))
            pos = &d->pos;
        if (!pos || pos->GetMapId() != bot->GetMapId() || (pos->GetPositionX() == 0.0f && pos->GetPositionY() == 0.0f))
            return false;
        x = pos->GetPositionX();
        y = pos->GetPositionY();
        return true;
    }

    float Dist(Sample const& a, Sample const& b)
    {
        float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    class RecoveryManager
    {
    public:
        static RecoveryManager& Instance()
        {
            static RecoveryManager instance;
            return instance;
        }

        void Update(uint32 diff)
        {
            _timer += diff;
            if (_timer < SAMPLE_MS)
                return;
            _timer = 0;

            time_t now = time(nullptr);
            for (auto const& [guid, bot] : RandomBots())
                if (bot && bot->IsInWorld())
                    Check(bot, now);

            // Forget the bots gone offline a while ago.
            if (now >= _pruneAt)
            {
                _pruneAt = now + 10 * MINUTE;
                for (auto itr = _tracks.begin(); itr != _tracks.end();)
                    itr = now - itr->second.lastSeen > 10 * MINUTE ? _tracks.erase(itr) : std::next(itr);
            }
        }

        std::string Status() const
        {
            return Acore::StringFormat("Stuck bots helped: {} standing still, {} in circles, {} back and forth, {} dying "
                "over and over, {} out of their level range; {} hearthstones. {} bots watched{}.", _counters.stuck,
                _counters.loops, _counters.noProgress, _counters.deaths, _counters.outOfRange, _counters.hearths, _tracks.size(), settings.enabled ? "" : " (switched off)");
        }

    private:
        void Check(Player* bot, time_t now)
        {
            PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
            if (!botAI || botAI->GetMaster() || bot->InBattleground() || bot->InArena() ||
                (bot->GetMap() && bot->GetMap()->Instanceable()) || bot->IsInFlight() || bot->IsBeingTeleported())
                return;
            // Held where it is by an event: standing still is its job (the city duels' bots were hearthed home).
            if (IsBorrowedBot(bot->GetGUID()) || CityDuels::IsDuellist(bot->GetGUID()) ||
                RandomBotLevelMgr::IsHeld(bot->GetGUID()))
            {
                _tracks.erase(bot->GetGUID().GetCounter());
                return;
            }

            BotTrack& track = _tracks[bot->GetGUID().GetCounter()];
            track.lastSeen = now;

            // Deaths: counted when the bot is first seen dead.
            bool dead = !bot->IsAlive();
            if (dead && !track.wasDead)
            {
                track.deaths.push_back(now);
                while (!track.deaths.empty() && now - track.deaths.front() > time_t(settings.deathsWindowMinutes) * MINUTE)
                    track.deaths.pop_front();
                if (track.deaths.size() >= settings.deathsLimit)
                {
                    track.deaths.clear();
                    ++_counters.deaths;
                    CoaWorldState::Recovery(bot->GetGUID().GetCounter(), false), CoaWorld::LogEvent("stuck", bot->GetName(), "dying over and over");
                    track.wasDead = dead;
                    // Back home once it is up again.
                    track.lastRecovery = now - RELAPSE_SECONDS / 2;
                    _hearthAfterRevive.insert(bot->GetGUID().GetCounter());
                    return;
                }
            }
            track.wasDead = dead;
            if (dead)
                return;

            if (_hearthAfterRevive.erase(bot->GetGUID().GetCounter()))
            {
                Hearth(bot, botAI, track, now);
                return;
            }

            if (OutOfRange(bot, botAI, track, now))
                return;

            NewRpgStatus status = botAI->rpgInfo.GetStatus();
            // Fighting, looting, eating or drinking, casting (a gathering, a long spell): busy, not stuck; nor for
            // BUSY_SECONDS after (the laptop's a31 counted 1108 detections on 639 of 976 bots in 34 min).
            if (bot->IsInCombat() || !bot->GetLootGUID().IsEmpty() || bot->IsSitState() || bot->IsNonMeleeSpellCast(false))
                track.busyAt = now;
            if (bot->IsInCombat() || !IsTravelling(status) || now - track.busyAt < BUSY_SECONDS)
            {
                track.samples.clear();
                track.bestAt = 0;
                return;
            }

            track.samples.push_back({ now, bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ() });
            uint32 keep = std::max(settings.stuckSeconds, settings.loopSeconds);
            while (!track.samples.empty() && now - track.samples.front().when > time_t(keep) + 15)
                track.samples.pop_front();
            if (track.samples.front().mapId != bot->GetMapId())
            {
                track.samples.clear();
                track.bestAt = 0;
                return;
            }

            Sample const& last = track.samples.back();

            // Standing still for stuckSeconds.
            bool still = false;
            if (now - track.samples.front().when >= time_t(settings.stuckSeconds))
            {
                still = true;
                for (Sample const& s : track.samples)
                    if (now - s.when <= time_t(settings.stuckSeconds) && Dist(s, last) > 3.0f)
                        still = false;
            }

            // Round in circles for loopSeconds.
            bool loop = false;
            if (!still && now - track.samples.front().when >= time_t(settings.loopSeconds))
            {
                float walked = 0.0f;
                for (size_t i = 1; i < track.samples.size(); ++i)
                    walked += Dist(track.samples[i - 1], track.samples[i]);
                loop = walked > 120.0f && Dist(track.samples.front(), last) < 15.0f;
            }

            // Back and forth: walking, no nearer the destination for noProgressSeconds (still 30 yards away or more).
            bool pacing = false;
            float destX, destY;
            if (settings.noProgressSeconds && Destination(bot, botAI, destX, destY))
            {
                float dist = std::hypot(bot->GetPositionX() - destX, bot->GetPositionY() - destY);
                if (destX != track.destX || destY != track.destY || !track.bestAt)
                {
                    track.destX = destX;
                    track.destY = destY;
                    track.bestDist = dist;
                    track.bestAt = now;
                    track.walkedSinceBest = 0.0f;
                }
                else
                {
                    if (track.samples.size() >= 2)
                        track.walkedSinceBest += Dist(track.samples[track.samples.size() - 2], last);
                    if (dist < track.bestDist - 5.0f)
                    {
                        track.bestDist = dist;
                        track.bestAt = now;
                        track.walkedSinceBest = 0.0f;
                    }
                    else
                        pacing = !still && !loop && dist > 30.0f && track.walkedSinceBest > 30.0f &&
                                 now - track.bestAt >= time_t(settings.noProgressSeconds);
                }
            }
            else
                track.bestAt = 0;

            if (!still && !loop && !pacing)
                return;

            CoaWorldState::Recovery(bot->GetGUID().GetCounter(), false), CoaWorld::LogEvent("stuck", bot->GetName(), still ? "standing still on its way"
                                                       : loop ? "going round in circles" : "back and forth, no nearer");
            if (still)
                ++_counters.stuck;
            else if (loop)
                ++_counters.loops;
            else
                ++_counters.noProgress;
            track.samples.clear();
            track.bestAt = 0;

            Quarantine(bot, botAI);

            // The third time within ten minutes of each other: home. Before: something else to do.
            track.detections = track.lastRecovery && now - track.lastRecovery < RELAPSE_SECONDS ? track.detections + 1 : 1;
            if (track.detections >= HEARTH_AT)
            {
                track.detections = 0;
                Hearth(bot, botAI, track, now);
            }
            else
            {
                botAI->rpgInfo.ChangeToIdle();
                track.lastRecovery = now;
            }
        }

        // A bot far above the zone it walks in, for outOfRangeMinutes: to a place of its level, unseen, or by
        // hearthstone after twice the time when players stay near. True when it was sent.
        bool OutOfRange(Player* bot, PlayerbotAI* botAI, BotTrack& track, time_t now)
        {
            uint32 const zone = bot->GetZoneId();
            if (!settings.outOfRangeMinutes || bot->GetGroup() || bot->IsInCombat() ||
                !CoaWorldZones::FarAbove(zone, bot->GetLevel(), uint8(settings.outOfRangeLevels)))
            {
                track.outSince = 0;
                return false;
            }
            if (!track.outSince || track.outZone != zone)
            {
                track.outZone = zone;
                track.outSince = now;
                return false;
            }
            time_t const waited = now - track.outSince;
            if (waited < time_t(settings.outOfRangeMinutes) * MINUTE)
                return false;

            bool const seen = botAI->HasPlayerNearby(150.0f);
            if (seen && waited < 2 * time_t(settings.outOfRangeMinutes) * MINUTE)
                return false;

            ++_counters.outOfRange;
            track.outSince = 0;
            track.samples.clear();
            track.bestAt = 0;
            CoaWorldState::Recovery(bot->GetGUID().GetCounter(), false), CoaWorld::LogEvent("stuck", bot->GetName(), Acore::StringFormat("level {} in a zone far below it ({})",
                bot->GetLevel(), zone));
            if (seen)
            {
                Hearth(bot, botAI, track, now);
                return true;
            }
            botAI->rpgInfo.ChangeToIdle();
            bot->GetMotionMaster()->Clear();
            bot->StopMoving();
            sRandomPlayerbotMgr.RandomTeleportForLevel(bot);
            CoaWorldState::Recovery(bot->GetGUID().GetCounter(), true), CoaWorld::LogEvent("unstuck", bot->GetName(), "sent to a place of its level");
            return true;
        }

        void Hearth(Player* bot, PlayerbotAI* botAI, BotTrack& track, time_t now)
        {
            track.lastRecovery = now;
            botAI->rpgInfo.ChangeToIdle();
            if (!settings.hearth || bot->IsInCombat())
                return;

            bot->GetMotionMaster()->Clear();
            bot->StopMoving();
            // The real hearthstone, its cast seen by whoever is near; no item or cooldown needed.
            BotLine(bot, "recovery_home", {});
            bot->CastSpell(bot, SPELL_HEARTHSTONE, TriggerCastFlags(TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD |
                TRIGGERED_IGNORE_POWER_AND_REAGENT_COST | TRIGGERED_IGNORE_CAST_ITEM));
            botAI->rpgInfo.ChangeToRest();
            ++_counters.hearths;
            CoaWorldState::Recovery(bot->GetGUID().GetCounter(), true), CoaWorld::LogEvent("unstuck", bot->GetName(), "hearthstone home");
        }

        std::unordered_map<uint32, BotTrack> _tracks;
        std::unordered_set<uint32> _hearthAfterRevive;
        Counters _counters;
        uint32 _timer = 0;
        time_t _pruneAt = 0;
    };

    RecoveryManager& Recovery() { return RecoveryManager::Instance(); }

    void LoadSettings()
    {
        auto key = [](char const* name) { return std::string(PREFIX) + name; };
        settings.enabled = sConfigMgr->GetOption<bool>(key("Enable"), false, false);
        settings.stuckSeconds = std::clamp<uint32>(sConfigMgr->GetOption<uint32>(key("StuckSeconds"), 90, false), 30, 1800);
        settings.loopSeconds = std::clamp<uint32>(sConfigMgr->GetOption<uint32>(key("LoopSeconds"), 180, false), 60, 1800);
        settings.deathsLimit = std::clamp<uint32>(sConfigMgr->GetOption<uint32>(key("DeathsLimit"), 3, false), 2, 20);
        settings.deathsWindowMinutes = std::clamp<uint32>(sConfigMgr->GetOption<uint32>(key("DeathsWindowMinutes"), 15, false), 1, 120);
        settings.hearth = sConfigMgr->GetOption<bool>(key("Hearth"), true, false);
        settings.quarantineMinutes = std::min<uint32>(1440, sConfigMgr->GetOption<uint32>(key("QuarantineMinutes"), 30, false));
        uint32 noProgress = sConfigMgr->GetOption<uint32>(key("NoProgressSeconds"), 120, false);
        settings.noProgressSeconds = noProgress ? std::clamp<uint32>(noProgress, 30, 1800) : 0;
        settings.outOfRangeMinutes = std::min<uint32>(120, sConfigMgr->GetOption<uint32>(key("OutOfRangeMinutes"), 5, false));
        settings.outOfRangeLevels = std::clamp<uint32>(sConfigMgr->GetOption<uint32>(key("OutOfRangeLevels"), 8, false), 3, 40);
    }
}

bool CoaWorldQuestQuarantined(uint32 botLowGuid, uint32 questId)
{
    if (!g_anyQuarantine.load(std::memory_order_relaxed))
        return false;
    std::lock_guard<std::mutex> lock(g_quarantineLock);
    auto itr = g_quarantine.find(QuarantineKey(botLowGuid, questId));
    return itr != g_quarantine.end() && itr->second > time(nullptr);
}

class CoaWorldRecoveryWorldScript : public WorldScript
{
public:
    CoaWorldRecoveryWorldScript()
        : WorldScript("CoaWorldRecoveryWorldScript", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE })
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
        if (settings.enabled)
            Recovery().Update(diff);
    }
};

class CoaWorldRecoveryCommandScript : public CommandScript
{
public:
    CoaWorldRecoveryCommandScript() : CommandScript("CoaWorldRecoveryCommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable recoveryTable = {
            { "status", HandleStatus, SEC_GAMEMASTER, Console::Yes },
        };
        static ChatCommandTable worldTable = {
            { "recovery", recoveryTable },
        };
        static ChatCommandTable commandTable = {
            { "coaworld", worldTable },
        };
        return commandTable;
    }

    static bool HandleStatus(ChatHandler* handler)
    {
        handler->SendSysMessage(Recovery().Status());
        return true;
    }
};

void AddSC_coa_world_recovery()
{
    new CoaWorldRecoveryWorldScript();
    new CoaWorldRecoveryCommandScript();
}
