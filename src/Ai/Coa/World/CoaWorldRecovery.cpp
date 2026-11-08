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
 *  - dying over and over: DeathsLimit deaths within DeathsWindowMinutes.
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

#include "CoaWorldBots.h"

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

    struct Settings
    {
        bool enabled = false;
        uint32 stuckSeconds = 90;
        uint32 loopSeconds = 180;
        uint32 deathsLimit = 3;
        uint32 deathsWindowMinutes = 15;
        bool hearth = true;
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
    };

    struct Counters
    {
        uint32 stuck = 0;
        uint32 loops = 0;
        uint32 deaths = 0;
        uint32 hearths = 0;
    };

    // RPG states in which a bot is meant to be going somewhere.
    bool IsTravelling(NewRpgStatus status)
    {
        return status == RPG_GO_GRIND || status == RPG_GO_CAMP || status == RPG_DO_QUEST || status == RPG_GO_CITY;
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
            for (auto const& [guid, bot] : sRandomPlayerbotMgr.GetAllBots())
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
            return Acore::StringFormat("Stuck bots helped: {} standing still, {} in circles, {} dying over and over; "
                "{} hearthstones. {} bots watched{}.", _counters.stuck, _counters.loops, _counters.deaths,
                _counters.hearths, _tracks.size(), settings.enabled ? "" : " (switched off)");
        }

    private:
        void Check(Player* bot, time_t now)
        {
            PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
            if (!botAI || botAI->GetMaster() || bot->InBattleground() || bot->InArena() ||
                (bot->GetMap() && bot->GetMap()->Instanceable()) || bot->IsInFlight() || bot->IsBeingTeleported())
                return;

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

            NewRpgStatus status = botAI->rpgInfo.GetStatus();
            if (bot->IsInCombat() || !IsTravelling(status))
            {
                track.samples.clear();
                return;
            }

            track.samples.push_back({ now, bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ() });
            uint32 keep = std::max(settings.stuckSeconds, settings.loopSeconds);
            while (!track.samples.empty() && now - track.samples.front().when > time_t(keep) + 15)
                track.samples.pop_front();
            if (track.samples.front().mapId != bot->GetMapId())
            {
                track.samples.clear();
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

            if (!still && !loop)
                return;

            if (still)
                ++_counters.stuck;
            else
                ++_counters.loops;
            track.samples.clear();

            // Again within ten minutes: home. Otherwise something else to do.
            if (track.lastRecovery && now - track.lastRecovery < RELAPSE_SECONDS)
                Hearth(bot, botAI, track, now);
            else
            {
                botAI->rpgInfo.ChangeToIdle();
                track.lastRecovery = now;
            }
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
            bot->CastSpell(bot, SPELL_HEARTHSTONE, TriggerCastFlags(TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD |
                TRIGGERED_IGNORE_POWER_AND_REAGENT_COST | TRIGGERED_IGNORE_CAST_ITEM));
            botAI->rpgInfo.ChangeToRest();
            ++_counters.hearths;
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
    }
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
