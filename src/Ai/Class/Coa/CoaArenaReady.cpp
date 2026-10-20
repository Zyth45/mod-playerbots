/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

// Arena ready pylon, as on the original CoA realm: one clickable object in each starting room. Clicking it marks
// the player ready; bots mark themselves ready once they are settled. When every combatant is ready the preparation
// countdown drops to a few seconds instead of running the full minute and a half.

#include "Battleground.h"
#include "Chat.h"
#include "Config.h"
#include "GameObject.h"
#include "GameTime.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "ScriptMgr.h"
#include "World.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace
{
    struct ArenaReadyState
    {
        std::unordered_set<ObjectGuid> ready;
        std::unordered_map<ObjectGuid, uint32> arrival;
        std::vector<ObjectGuid> pylons;
        uint32 announced = 0;
        int32 lastCount = 0;
        uint32 tick = 0;
        bool spawned = false;
        bool shortened = false;
    };

    struct KnownSpot
    {
        uint32 map;
        float x, y, z;
    };

    constexpr KnownSpot KnownSpots[] = {
        {559, 4088.977f, 2875.548f, 12.185f},
        {559, 4023.170f, 2966.881f, 12.165f},
        {562, 6287.254f, 287.938f, 5.281f},
        {572, 1275.171f, 1733.021f, 31.605f},
    };

    std::mutex StatesLock;
    std::unordered_map<uint32, ArenaReadyState> States;

    // Read once and again on ".reload config": these run every second in each arena, and a missing key
    // makes the core log a warning at every read.
    struct ArenaReadySettings
    {
        bool loaded = false;
        bool enabled = true;
        uint32 entry = 184615;
        uint32 display = 0;
        float distance = 6.0f;
        float side = 3.0f;
        float height = 1.2f;
        float scale = 0.5f;
        int32 countdownMs = 5 * IN_MILLISECONDS;
        int32 botSettleMs = 10 * IN_MILLISECONDS;
    } Settings;

    ArenaReadySettings const& Get()
    {
        if (!Settings.loaded)
        {
            Settings.enabled = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaArenaReady", true);
            Settings.entry = sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaArenaReadyEntry", 184615);
            Settings.display = sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaArenaReadyDisplay", 0);
            Settings.distance = sConfigMgr->GetOption<float>("AiPlayerbot.CoaArenaReadyDistance", 6.0f);
            Settings.side = sConfigMgr->GetOption<float>("AiPlayerbot.CoaArenaReadySide", 3.0f);
            Settings.height = sConfigMgr->GetOption<float>("AiPlayerbot.CoaArenaReadyHeight", 1.2f);
            Settings.scale = sConfigMgr->GetOption<float>("AiPlayerbot.CoaArenaReadyScale", 0.5f);
            Settings.countdownMs = int32(sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaArenaReadyCountdown", 5)) * IN_MILLISECONDS;
            Settings.botSettleMs = int32(sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaArenaReadyBotDelay", 10)) * IN_MILLISECONDS;
            Settings.loaded = true;
        }
        return Settings;
    }

    bool Enabled() { return Get().enabled; }
    uint32 PylonEntry() { return Get().entry; }
    uint32 PylonDisplay() { return Get().display; }
    float PylonDistance() { return Get().distance; }
    float PylonSide() { return Get().side; }
    float PylonHeight() { return Get().height; }
    float PylonScale() { return Get().scale; }
    int32 CountdownMs() { return Get().countdownMs; }
    int32 BotSettleMs() { return Get().botSettleMs; }

    void Announce(Battleground* bg, std::string const& text, ChatMsg type = CHAT_MSG_RAID_BOSS_EMOTE)
    {
        WorldPacket data;
        ChatHandler::BuildChatPacket(data, type, LANG_UNIVERSAL, nullptr, nullptr, text);
        bg->SendPacketToAll(&data);
    }

    void Countdown(Battleground* bg, ArenaReadyState& state)
    {
        int32 const left = (bg->GetStartDelayTime() + IN_MILLISECONDS - 1) / IN_MILLISECONDS;
        if (left < 1 || left > 5 || left == state.lastCount)
            return;
        state.lastCount = left;
        Announce(bg, std::to_string(left), CHAT_MSG_RAID_WARNING);
    }

    uint32 Expected(Battleground* bg) { return uint32(bg->GetArenaType()) * 2; }

    uint32 ReadyPresent(Battleground* bg, ArenaReadyState const& state)
    {
        uint32 count = 0;
        for (auto const& [guid, player] : bg->GetPlayers())
            if (state.ready.count(guid))
                ++count;
        return count;
    }

    void Despawn(Battleground* bg, ArenaReadyState& state)
    {
        if (BattlegroundMap* map = bg->FindBgMap())
            for (ObjectGuid const& guid : state.pylons)
                if (GameObject* go = map->GetGameObject(guid))
                {
                    go->SetRespawnTime(0);
                    go->Delete();
                }
        state.pylons.clear();
    }

    void SpawnPylons(Battleground* bg, ArenaReadyState& state)
    {
        BattlegroundMap* map = bg->FindBgMap();
        if (!map)
            return;
        state.spawned = true;
        uint32 const display = PylonDisplay();
        Position const* starts[2] = { bg->GetTeamStartPosition(TEAM_ALLIANCE), bg->GetTeamStartPosition(TEAM_HORDE) };
        KnownSpot const* nearest[2] = { nullptr, nullptr };
        for (int team = 0; team < 2; ++team)
            for (KnownSpot const& spot : KnownSpots)
                if (spot.map == bg->GetMapId() && starts[team] &&
                    (!nearest[team] || starts[team]->GetExactDist2d(spot.x, spot.y) <
                                           starts[team]->GetExactDist2d(nearest[team]->x, nearest[team]->y)))
                    nearest[team] = &spot;
        bool const bothKnown = nearest[0] && nearest[1] && nearest[0] != nearest[1];
        KnownSpot const* known = nullptr;
        int knownTeam = -1;
        float knownForward = 0.0f, knownLeft = 0.0f;
        if (!bothKnown && nearest[0] && starts[0] && starts[1])
        {
            known = nearest[0];
            knownTeam = starts[0]->GetExactDist2d(known->x, known->y) <= starts[1]->GetExactDist2d(known->x, known->y) ? 0 : 1;
            Position const* own = starts[knownTeam];
            float const a = own->GetAbsoluteAngle(starts[1 - knownTeam]);
            float const dx = known->x - own->GetPositionX();
            float const dy = known->y - own->GetPositionY();
            knownForward = dx * std::cos(a) + dy * std::sin(a);
            knownLeft = -dx * std::sin(a) + dy * std::cos(a);
        }
        for (int team = 0; team < 2; ++team)
        {
            Position const* start = starts[team];
            if (!start || (!start->GetPositionX() && !start->GetPositionY()))
                continue;
            Position const* other = starts[1 - team];
            float o = start->GetOrientation();
            if (other && (other->GetPositionX() || other->GetPositionY()))
                o = start->GetAbsoluteAngle(other);
            float x, y, z;
            if (bothKnown || team == knownTeam)
            {
                KnownSpot const* spot = bothKnown ? nearest[team] : known;
                x = spot->x;
                y = spot->y;
                z = spot->z;
            }
            else
            {
                float forward = PylonDistance();
                float left = -PylonSide();
                if (knownTeam >= 0)
                {
                    forward = knownForward;
                    left = knownLeft;
                }
                x = start->GetPositionX() + forward * std::cos(o) - left * std::sin(o);
                y = start->GetPositionY() + forward * std::sin(o) + left * std::cos(o);
                z = map->GetHeight(x, y, start->GetPositionZ() + 2.0f);
                if (z <= INVALID_HEIGHT)
                    z = start->GetPositionZ();
            }
            z += PylonHeight();
            GameObject* go = map->SummonGameObject(PylonEntry(), x, y, z, o, 0.0f, 0.0f, 0.0f, 0.0f, 0, false);
            if (!go)
                continue;
            if (display)
                go->SetDisplayId(display);
            go->SetObjectScale(PylonScale());
            go->SetUInt32Value(GAMEOBJECT_FACTION, 0);
            go->RemoveGameObjectFlag(GameObjectFlags(GO_FLAG_LOCKED | GO_FLAG_INTERACT_COND | GO_FLAG_NOT_SELECTABLE));
            state.pylons.push_back(go->GetGUID());
        }
    }

    void Evaluate(Battleground* bg, ArenaReadyState& state)
    {
        uint32 const expected = Expected(bg);
        uint32 const ready = ReadyPresent(bg, state);
        if (ready != state.announced)
        {
            state.announced = ready;
            Announce(bg, "Players Ready: " + std::to_string(ready) + "/" + std::to_string(expected));
        }
        if (state.shortened || !expected || ready < expected || bg->GetPlayersSize() < expected)
            return;
        state.shortened = true;
        int32 const countdown = CountdownMs();
        if (bg->GetStartDelayTime() <= countdown)
            return;
        bg->SetStartDelayTime(countdown);
        bg->AddStartingEventFlag(BG_STARTING_EVENT_1 | BG_STARTING_EVENT_2 | BG_STARTING_EVENT_3);
        Announce(bg, "Prepare for Battle! Arena will start in " + std::to_string(countdown / IN_MILLISECONDS) +
                         " Seconds!");
        LOG_INFO("playerbots.coa", "coa arena ready: arena {} all {} ready, start in {} s", bg->GetInstanceID(),
                 expected, countdown / IN_MILLISECONDS);
    }
}

class CoaArenaReadyBattleground : public AllBattlegroundScript
{
public:
    CoaArenaReadyBattleground()
        : AllBattlegroundScript("coa_arena_ready_bg",
                                { ALLBATTLEGROUNDHOOK_ON_BATTLEGROUND_UPDATE, ALLBATTLEGROUNDHOOK_ON_BATTLEGROUND_DESTROY,
                                  ALLBATTLEGROUNDHOOK_ON_BATTLEGROUND_REMOVE_PLAYER_AT_LEAVE,
                                  ALLBATTLEGROUNDHOOK_ON_BATTLEGROUND_END,
                                  ALLBATTLEGROUNDHOOK_ON_BATTLEGROUND_ADD_PLAYER })
    {
    }

    void OnBattlegroundUpdate(Battleground* bg, uint32 diff) override
    {
        if (!bg || !bg->isArena())
            return;

        std::lock_guard<std::mutex> lock(StatesLock);
        auto found = States.find(bg->GetInstanceID());
        if (bg->GetStatus() != STATUS_WAIT_JOIN)
        {
            if (found != States.end())
            {
                Despawn(bg, found->second);
                States.erase(found);
            }
            return;
        }
        if (!Enabled() || !bg->GetPlayersSize())
            return;

        ArenaReadyState& state = found != States.end() ? found->second : States[bg->GetInstanceID()];
        state.tick += diff;
        if (state.tick < 1000)
            return;
        state.tick = 0;

        if (!state.spawned)
            SpawnPylons(bg, state);

        Countdown(bg, state);

        uint32 const now = GameTime::GetGameTimeMS().count();
        // Nobody should be dead before the gates open: a combatant who arrives dead or falls in the starting
        // room would stay a corpse for the whole match.
        for (auto const& [guid, player] : bg->GetPlayers())
            if (player && player->IsInWorld() && !player->IsAlive() && !player->IsBeingTeleported())
            {
                LOG_INFO("playerbots.coa", "coa arena ready: {} dead in the starting room of arena {} at {:.1f} {:.1f} {:.1f}, raised",
                         player->GetName(), bg->GetInstanceID(), player->GetPositionX(), player->GetPositionY(),
                         player->GetPositionZ());
                player->ResurrectPlayer(1.0f);
                player->SpawnCorpseBones();
            }
        for (auto const& [guid, player] : bg->GetPlayers())
        {
            if (state.ready.count(guid) || !player || !player->IsInWorld() || !player->IsAlive())
                continue;
            auto arrived = state.arrival.find(guid);
            if (arrived == state.arrival.end())
                arrived = state.arrival.emplace(guid, now).first;
            int32 const elapsed = int32(now - arrived->second);
            bool const settled = elapsed >= BotSettleMs();
            bool const overdue = elapsed >= BotSettleMs() + 12 * IN_MILLISECONDS;
            if (!settled)
                continue;
            if (!GET_PLAYERBOT_AI(player) || player->IsBeingTeleported() || (!overdue && player->IsNonMeleeSpellCast(false)))
                continue;
            state.ready.insert(guid);
        }

        Evaluate(bg, state);
    }

    void OnBattlegroundAddPlayer(Battleground* bg, Player* player) override
    {
        if (!bg || !player || !bg->isArena())
            return;
        // Solo queues mix the factions in one team: a combatant on the other faction's side takes that side's player
        // faction for the match, so his team mates see him friendly (the arena is free for all, foes stay hostile).
        if (player->GetTeamId(true) != player->GetBgTeamId())
            player->SetFaction(player->GetBgTeamId() == TEAM_ALLIANCE ? 1 : 2);
        {
            std::lock_guard<std::mutex> lock(StatesLock);
            States[bg->GetInstanceID()].arrival[player->GetGUID()] = GameTime::GetGameTimeMS().count();
        }
        LOG_INFO("playerbots.coa",
                 "coa arena join: {} ({}) arena {} map {} type {} status {}, arena side {}, faction {}, alliance {} "
                 "horde {} in the arena, alive {}, health {}/{}, at {:.1f} {:.1f} {:.1f}",
                 player->GetName(), GET_PLAYERBOT_AI(player) ? "bot" : "player", bg->GetInstanceID(), bg->GetMapId(),
                 uint32(bg->GetArenaType()), uint32(bg->GetStatus()), uint32(player->GetBgTeamId()),
                 uint32(player->GetTeamId(true)), bg->GetPlayersCountByTeam(TEAM_ALLIANCE),
                 bg->GetPlayersCountByTeam(TEAM_HORDE), player->IsAlive(), player->GetHealth(), player->GetMaxHealth(),
                 player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
    }

    void OnBattlegroundRemovePlayerAtLeave(Battleground* bg, Player* player) override
    {
        if (!bg || !player || !bg->isArena())
            return;
        if (player->GetTeamId(true) != player->GetBgTeamId())
            player->SetFactionForRace(player->getRace(true));
        LOG_INFO("playerbots.coa",
                 "coa arena leave: {} ({}) arena {} map {} type {} status {} after {} s, alive {}, health {}/{}, "
                 "in world {}, teleporting {}",
                 player->GetName(), GET_PLAYERBOT_AI(player) ? "bot" : "player", bg->GetInstanceID(), bg->GetMapId(),
                 uint32(bg->GetArenaType()), uint32(bg->GetStatus()), bg->GetStartTime() / IN_MILLISECONDS,
                 player->IsAlive(), player->GetHealth(), player->GetMaxHealth(), player->IsInWorld(),
                 player->IsBeingTeleported());
    }

    void OnBattlegroundEnd(Battleground* bg, TeamId winner) override
    {
        if (!bg || !bg->isArena())
            return;
        std::string roster;
        for (auto const& [guid, player] : bg->GetPlayers())
            if (player)
                roster += " " + player->GetName() + (player->IsAlive() ? "(alive)" : "(dead)");
        LOG_INFO("playerbots.coa", "coa arena end: arena {} map {} type {} winner {} after {} s, alliance alive {}, "
                 "horde alive {}, players{}",
                 bg->GetInstanceID(), bg->GetMapId(), uint32(bg->GetArenaType()), uint32(winner),
                 bg->GetStartTime() / IN_MILLISECONDS, bg->GetAlivePlayersCountByTeam(TEAM_ALLIANCE),
                 bg->GetAlivePlayersCountByTeam(TEAM_HORDE), roster);
    }

    void OnBattlegroundDestroy(Battleground* bg) override
    {
        if (!bg)
            return;
        std::lock_guard<std::mutex> lock(StatesLock);
        States.erase(bg->GetInstanceID());
    }
};

class CoaArenaReadyPylon : public AllGameObjectScript
{
public:
    CoaArenaReadyPylon() : AllGameObjectScript("coa_arena_ready_pylon") {}

    bool CanGameObjectGossipHello(Player* player, GameObject* go) override
    {
        if (!player || !go || go->GetEntry() != PylonEntry())
            return false;
        Battleground* bg = player->GetBattleground();
        if (!bg || !bg->isArena() || bg->GetStatus() != STATUS_WAIT_JOIN)
            return false;

        std::lock_guard<std::mutex> lock(StatesLock);
        auto found = States.find(bg->GetInstanceID());
        if (found == States.end())
            return false;
        ArenaReadyState& state = found->second;
        if (std::find(state.pylons.begin(), state.pylons.end(), go->GetGUID()) == state.pylons.end())
            return false;

        if (!state.ready.insert(player->GetGUID()).second)
        {
            ChatHandler(player->GetSession()).SendSysMessage("You are already ready.");
            return true;
        }
        Evaluate(bg, state);
        return true;
    }
};

void CoaPvpOptionsReload();

namespace
{
    std::string ArenaRoster(Battleground* bg)
    {
        std::string roster;
        for (auto const& [guid, player] : bg->GetPlayers())
            if (player)
                roster += " " + player->GetName() + "(side " + std::to_string(uint32(player->GetBgTeamId())) +
                          (player->IsAlive() ? " alive " : " dead ") + std::to_string(player->GetHealth()) + "/" +
                          std::to_string(player->GetMaxHealth()) + " form " +
                          std::to_string(uint32(player->GetShapeshiftForm())) + ")";
        return roster;
    }
}

// Why an arena ends: the state of everyone when the core weighs the winner, and each player kill.
class CoaArenaEndWatch : public ArenaScript
{
public:
    CoaArenaEndWatch() : ArenaScript("coa_arena_end_watch", { ARENAHOOK_ON_BEFORE_CHECK_WIN_CONDITION }) {}

    bool OnBeforeArenaCheckWinConditions(Battleground* const bg) override
    {
        if (bg && bg->GetStatus() == STATUS_IN_PROGRESS &&
            (!bg->GetAlivePlayersCountByTeam(TEAM_ALLIANCE) || !bg->GetAlivePlayersCountByTeam(TEAM_HORDE)))
            LOG_INFO("playerbots.coa", "coa arena win check: arena {} after {} s, alliance alive {}/{}, horde alive {}/{},{}",
                     bg->GetInstanceID(), bg->GetStartTime() / IN_MILLISECONDS, bg->GetAlivePlayersCountByTeam(TEAM_ALLIANCE),
                     bg->GetPlayersCountByTeam(TEAM_ALLIANCE), bg->GetAlivePlayersCountByTeam(TEAM_HORDE),
                     bg->GetPlayersCountByTeam(TEAM_HORDE), ArenaRoster(bg));
        return true;
    }
};

class CoaArenaKillWatch : public PlayerScript
{
public:
    CoaArenaKillWatch() : PlayerScript("coa_arena_kill_watch", { PLAYERHOOK_ON_PVP_KILL }) {}

    void OnPlayerPVPKill(Player* killer, Player* killed) override
    {
        if (!killer || !killed || !killed->InArena())
            return;
        LOG_INFO("playerbots.coa", "coa arena kill: {} killed {} (alive {}, health {}/{}, form {})", killer->GetName(),
                 killed->GetName(), killed->IsAlive(), killed->GetHealth(), killed->GetMaxHealth(),
                 uint32(killed->GetShapeshiftForm()));
    }
};

class CoaArenaReadySettings : public WorldScript
{
public:
    CoaArenaReadySettings() : WorldScript("coa_arena_ready_settings", { WORLDHOOK_ON_AFTER_CONFIG_LOAD }) {}

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        Settings.loaded = false;
        CoaPvpOptionsReload();
    }
};

void AddSC_coa_arena_ready()
{
    new CoaArenaReadySettings();
    new CoaArenaEndWatch();
    new CoaArenaKillWatch();
    new CoaArenaReadyBattleground();
    new CoaArenaReadyPylon();
}
