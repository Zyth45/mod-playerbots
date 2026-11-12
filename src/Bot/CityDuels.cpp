/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CityDuels.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "Config.h"
#include "LFGMgr.h"
#include "Log.h"
#include "Map.h"
#include "MapMgr.h"
#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RandomBotLevelMgr.h"
#include "RandomPlayerbotMgr.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "WorldSessionMgr.h"

namespace
{
constexpr uint32 DuelSpell = 7266;
constexpr float PairSpacing = 10.0f;     // between two pairs, along the row
constexpr float Apart = 6.0f;            // between the two duellists of a pair: the duel spell reaches 10 yd
constexpr float HostileRange = 40.0f;    // a real player of the other faction this close: no new duel
constexpr time_t ArriveSeconds = 4;      // after the teleport, before the first challenge
constexpr time_t ChallengeSeconds = 12;  // the duel has to be on by then
constexpr time_t DuelMaxSeconds = 180;   // a duel still going after that is called off
constexpr time_t StuckSeconds = 45;      // a bot not at its spot by then is given up
constexpr time_t RetryFillSeconds = 60;  // no bot found for a pair: the next search waits that long
constexpr uint32 MaxFailures = 3;        // challenges that came to nothing before the pair is given up
constexpr uint32 MaxPlaced = 6;          // put back at its spot that many times without a duel: given up

enum class Phase : uint8
{
    Arriving,
    Resting,
    Challenged,
    Fighting
};

struct Pair
{
    ObjectGuid a, b;
    WorldLocation originA, originB;  // where each one was taken from, it goes back there
    uint32 slot = 0;
    Phase phase = Phase::Arriving;
    time_t created = 0;
    time_t phaseAt = 0;
    uint32 rounds = 0;
    uint32 failures = 0;
    uint32 placed = 0;
};

struct City
{
    char const* name;
    char const* option;
    TeamId team;
    WorldLocation fallback;
    std::vector<Pair> pairs;
    time_t nextFill = 0;
    bool warned = false;
};

struct Settings
{
    bool enable = true;
    uint32 pairs = 3;
    std::vector<uint32> levels;
    uint32 levelGap = 2;
    uint32 minLevel = 10;
    time_t rotation = 20 * MINUTE;
    time_t rest = 15;
    uint32 interval = 5 * IN_MILLISECONDS;
};

// Outside the main gates, on the road; a row of pairs runs across the road (the orientation is the row's
// direction). AiPlayerbot.CityDuels.OrgrimmarSpot / StormwindSpot replace them: "map x y z orientation".
// Orgrimmar: the grunts of the front gate stand at x 1384-1402 facing south, the spot is 45 yd out in Durotar.
// Stormwind: the gate guards stand on the bridge at x -9062..-9095, the spot is past the bridge in Elwynn Forest.
std::vector<City> Cities = {
    {"Orgrimmar", "AiPlayerbot.CityDuels.OrgrimmarSpot", TEAM_HORDE, WorldLocation(1, 1340.0f, -4372.0f, 27.0f, 1.73f)},
    {"Stormwind", "AiPlayerbot.CityDuels.StormwindSpot", TEAM_ALLIANCE, WorldLocation(0, -9150.0f, 365.0f, 90.0f, 2.36f)},
};

std::mutex DuellistsLock;
std::set<ObjectGuid> Duellists;  // read by RandomPlayerbotMgr::ProcessBot
uint32 Timer = 0;
uint32 Interval = 5 * IN_MILLISECONDS;  // AiPlayerbot.CityDuels.UpdateSeconds, read at each update
bool Running = false;  // pairs exist: Enable = 0 releases them once

// --- CoA World (CoA Monde session) ---
// Released bots that could not be sent back at once (dead, in a duel someone else started, mid-teleport): sent back
// as soon as they can, for PendingBackSeconds. Before, they stayed at the spot and wandered off in Elwynn or Durotar
// at level 60.
constexpr time_t PendingBackSeconds = 15 * MINUTE;
struct PendingBack
{
    ObjectGuid guid;
    WorldLocation origin;
    time_t until;
};
std::vector<PendingBack> PendingBacks;
// --- end CoA World ---

void SetDuellist(ObjectGuid guid, bool on)
{
    std::lock_guard<std::mutex> guard(DuellistsLock);
    if (on)
        Duellists.insert(guid);
    else
        Duellists.erase(guid);
}

Settings ReadSettings()
{
    Settings s;
    s.enable = sConfigMgr->GetOption<bool>("AiPlayerbot.CityDuels.Enable", true);
    s.pairs = std::min<uint32>(sConfigMgr->GetOption<uint32>("AiPlayerbot.CityDuels.BotsPerCity", 6), 12) / 2;
    std::istringstream list(sConfigMgr->GetOption<std::string>("AiPlayerbot.CityDuels.PairLevels", "60,60,30"));
    std::string one;
    while (std::getline(list, one, ','))
    {
        uint32 const level = uint32(std::strtoul(one.c_str(), nullptr, 10));
        if (level)
            s.levels.push_back(level);
    }
    if (s.levels.empty())
        s.levels.push_back(60);
    s.levelGap = sConfigMgr->GetOption<uint32>("AiPlayerbot.CityDuels.PairLevelGap", 2);
    s.minLevel = sConfigMgr->GetOption<uint32>("AiPlayerbot.CityDuels.MinLevel", 10);
    s.rotation = time_t(std::max<uint32>(sConfigMgr->GetOption<uint32>("AiPlayerbot.CityDuels.RotationMinutes", 20), 1)) * MINUTE;
    s.rest = time_t(sConfigMgr->GetOption<uint32>("AiPlayerbot.CityDuels.RestSeconds", 15));
    s.interval = std::max<uint32>(sConfigMgr->GetOption<uint32>("AiPlayerbot.CityDuels.UpdateSeconds", 5), 1) * IN_MILLISECONDS;
    return s;
}

WorldLocation CitySpot(City const& city)
{
    std::string const configured = sConfigMgr->GetOption<std::string>(city.option, "");
    if (!configured.empty())
    {
        std::istringstream in(configured);
        uint32 map = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f, o = 0.0f;
        if (in >> map >> x >> y >> z >> o)
            return WorldLocation(map, x, y, z, o);
    }
    return city.fallback;
}

// Where the duellist `first` or the other of the pair in `slot` stands: slot 0 at the spot, then one each side.
Position SlotPosition(WorldLocation const& spot, uint32 slot, bool first)
{
    float const along = float((slot + 1) / 2) * PairSpacing * ((slot % 2) ? 1.0f : -1.0f);
    float const row = spot.GetOrientation();
    float const across = row + float(M_PI) / 2.0f;
    float const half = (first ? -Apart : Apart) / 2.0f;
    float const x = spot.GetPositionX() + along * std::cos(row) + half * std::cos(across);
    float const y = spot.GetPositionY() + along * std::sin(row) + half * std::sin(across);
    float z = spot.GetPositionZ();
    if (Map* map = sMapMgr->FindMap(spot.GetMapId(), 0))
    {
        float const ground = map->GetHeight(PHASEMASK_NORMAL, x, y, z + 10.0f, true, 30.0f);
        if (ground > INVALID_HEIGHT)
            z = ground;
    }
    // Facing the other one.
    return Position(x, y, z, first ? across : across + float(M_PI));
}

Player* Find(ObjectGuid guid) { return guid ? ObjectAccessor::FindConnectedPlayer(guid) : nullptr; }

// A random bot nobody needs: online, on its own (no group, no master), out of battlegrounds, the dungeon finder
// and instances (Dungeon Clear runs in instances, with a group), not held by a test bench.
bool IsFree(Player* bot, TeamId team, uint32 minLevel)
{
    if (!bot || !bot->IsInWorld() || !bot->IsAlive() || bot->IsBeingTeleported() || bot->IsInFlight() ||
        bot->IsInCombat() || bot->GetGroup() || bot->duel || bot->GetTransport() || bot->GetTrader() ||
        bot->InBattleground() || bot->InBattlegroundQueue() || bot->IsGameMaster() || bot->IsDuringRemoveFromWorld() ||
        bot->GetTeamId() != team || bot->GetLevel() < minLevel)
        return false;
    if (!bot->GetSession() || bot->GetSession()->isLogingOut())
        return false;
    Map* map = bot->FindMap();
    if (!map || map->Instanceable())
        return false;
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI || botAI->GetMaster() || !sRandomPlayerbotMgr.IsRandomBot(bot))
        return false;
    if (RandomBotLevelMgr::IsHeld(bot->GetGUID()) || CityDuels::IsDuellist(bot->GetGUID()))
        return false;
    return sLFGMgr->GetState(bot->GetGUID()) == lfg::LFG_STATE_NONE;
}

// Still fit for the duels once taken: what IsFree asks, less what the duels themselves cause.
bool StillFit(Player* bot, uint32 mapId)
{
    if (!bot || !bot->GetSession() || bot->GetSession()->isLogingOut())
        return false;
    // Out of the world while a far teleport loads: judged once it is there.
    if (bot->IsBeingTeleported())
        return true;
    if (!bot->IsInWorld() || !bot->IsAlive() || bot->GetGroup() || bot->InBattleground() ||
        bot->InBattlegroundQueue() || bot->IsInFlight() || bot->GetTransport() || bot->IsDuringRemoveFromWorld())
        return false;
    if (bot->GetMapId() != mapId)
        return false;
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI || botAI->GetMaster())
        return false;
    return sLFGMgr->GetState(bot->GetGUID()) == lfg::LFG_STATE_NONE;
}

// Stays where it is put, does not wander, quest, queue or challenge anyone by itself, and leaves enemy players
// alone ("pvp" attacks them). It still accepts a duel and fights back.
void ApplyHold(Player* bot, bool stay)
{
    if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
        botAI->ChangeStrategy(std::string(stay ? "+stay," : "-stay,") +
                                  "-rpg,-new rpg,-grind,-travel,-quest,-gather,-loot,-mount,-start duel,-pvp,-lfg,-bg",
                              BOT_STATE_NON_COMBAT);
}

bool HoldLost(Player* bot)
{
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    return botAI && (!botAI->HasStrategy("stay", BOT_STATE_NON_COMBAT) ||
                     botAI->HasStrategy("start duel", BOT_STATE_NON_COMBAT) ||
                     botAI->HasStrategy("new rpg", BOT_STATE_NON_COMBAT) ||
                     botAI->HasStrategy("pvp", BOT_STATE_NON_COMBAT));
}

void Take(Player* bot, WorldLocation& origin)
{
    origin = WorldLocation(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                           bot->GetOrientation());
    SetDuellist(bot->GetGUID(), true);
    // Out of the level brackets' reach while it duels, as the test benches do.
    RandomBotLevelMgr::SetHeld(bot->GetGUID(), true);
    if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
    {
        botAI->ResetStrategies();
        ApplyHold(bot, true);
    }
}

void Place(Player* bot, WorldLocation const& spot, uint32 slot, bool first)
{
    Position const pos = SlotPosition(spot, slot, first);
    bot->TeleportTo(spot.GetMapId(), pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), pos.GetOrientation());
}

bool AtSpot(Player* bot, WorldLocation const& spot, uint32 slot, bool first)
{
    if (bot->IsBeingTeleported() || bot->GetMapId() != spot.GetMapId())
        return false;
    Position const pos = SlotPosition(spot, slot, first);
    return bot->GetExactDist2d(pos.GetPositionX(), pos.GetPositionY()) < 4.0f;
}

// Handed back to RandomPlayerbotMgr: its own strategies, and with `sendBack` the place it was taken from. A duel
// with someone else than its partner (a player challenged it) is left alone.
void Release(ObjectGuid guid, ObjectGuid partner, WorldLocation const& origin, bool sendBack)
{
    SetDuellist(guid, false);
    RandomBotLevelMgr::SetHeld(guid, false);
    Player* bot = Find(guid);
    if (!bot)
        return;
    bool const ownDuel = bot->duel && bot->duel->Opponent && bot->duel->Opponent->GetGUID() == partner;
    if (ownDuel)
        bot->DuelComplete(DUEL_INTERRUPTED);
    if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
        botAI->ResetStrategies();
    if (!sendBack || bot->GetGroup() || bot->InBattleground() || bot->InBattlegroundQueue() ||
        bot->IsDuringRemoveFromWorld() || !bot->GetSession() || bot->GetSession()->isLogingOut())
        return;
    // CoA World: not now, later (see PendingBacks).
    if (!bot->IsInWorld() || !bot->IsAlive() || bot->IsBeingTeleported() || bot->duel)
    {
        PendingBacks.push_back({ guid, origin, time(nullptr) + PendingBackSeconds });
        return;
    }
    // Only its duel's combat: a bot attacked by someone keeps fighting.
    if (bot->getAttackers().empty())
        bot->CombatStop(true);
    bot->TeleportTo(origin);
}

void ReleasePair(City const& city, Pair const& pair, bool sendBack, char const* why)
{
    LOG_DEBUG("playerbots", "city duels: {} pair in slot {} released after {} duels ({})", city.name, pair.slot,
              pair.rounds, why);
    Release(pair.a, pair.b, pair.originA, sendBack);
    Release(pair.b, pair.a, pair.originB, sendBack);
}

// A real player of the other faction near the spot: no duel starts, so that no area spell of a duel reaches him.
bool HostilePlayerNear(City const& city, WorldLocation const& spot)
{
    for (auto const& [id, session] : sWorldSessionMgr->GetAllSessions())
    {
        Player* player = session ? session->GetPlayer() : nullptr;
        if (!player || session->IsBot() || !IsRealPlayer(player) || !player->IsInWorld() ||
            player->GetMapId() != spot.GetMapId() || player->GetTeamId() == city.team)
            continue;
        if (player->GetExactDist2d(spot.GetPositionX(), spot.GetPositionY()) < HostileRange + PairSpacing * 3.0f)
            return true;
    }
    return false;
}

void Engage(Player* bot, Player* opponent)
{
    if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
    {
        ApplyHold(bot, false);
        botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Set(opponent);
    }
    bot->SetSelection(opponent->GetGUID());
    // In combat with each other at once: left out of combat, a bot at range may wait for the other to come.
    bot->SetInCombatWith(opponent);
    opponent->SetInCombatWith(bot);
    bot->Attack(opponent, true);
}

void Refresh(Player* bot)
{
    bot->SetFullHealth();
    Powers const power = bot->getPowerType();
    if (power == POWER_MANA || power == POWER_ENERGY)
        bot->SetPower(power, bot->GetMaxPower(power));
    // Damage stored for a later hit, built on the previous opponent (CoA Collect Bounty, as on the PvP bench).
    bot->RemoveAurasDueToSpell(504478);
    bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
}

// One pair, one step. False: the pair is over, released here.
bool UpdatePair(City& city, Pair& pair, WorldLocation const& spot, Settings const& settings, bool hostileNear,
                time_t now)
{
    Player* a = Find(pair.a);
    Player* b = Find(pair.b);
    if (!StillFit(a, spot.GetMapId()) || !StillFit(b, spot.GetMapId()))
    {
        ReleasePair(city, pair, true, "a bot left, died, grouped or queued");
        return false;
    }
    // Someone else challenged one of them (a player): that duel is theirs, the pair steps aside.
    for (Player* bot : {a, b})
        if (bot->duel && bot->duel->Opponent != (bot == a ? b : a))
        {
            ReleasePair(city, pair, true, "challenged by someone else");
            return false;
        }

    switch (pair.phase)
    {
        case Phase::Arriving:
        case Phase::Resting:
        {
            if (now - pair.created >= settings.rotation)
            {
                ReleasePair(city, pair, true, "rotation");
                return false;
            }
            for (Player* bot : {a, b})
                if (HoldLost(bot))
                    ApplyHold(bot, true);
            time_t const wait = pair.phase == Phase::Arriving ? ArriveSeconds : settings.rest;
            bool const teleporting = a->IsBeingTeleported() || b->IsBeingTeleported();
            if (now - pair.phaseAt < wait || teleporting)
            {
                if (teleporting && now - pair.phaseAt > wait + StuckSeconds)
                {
                    ReleasePair(city, pair, true, "stuck on a teleport");
                    return false;
                }
                return true;
            }
            // Back to its place after a duel, or when the game moved it.
            bool const aHere = AtSpot(a, spot, pair.slot, true), bHere = AtSpot(b, spot, pair.slot, false);
            if (!aHere || !bHere)
            {
                if (++pair.placed > MaxPlaced)
                {
                    ReleasePair(city, pair, true, "cannot stand at its spot");
                    return false;
                }
                if (!aHere)
                    Place(a, spot, pair.slot, true);
                if (!bHere)
                    Place(b, spot, pair.slot, false);
                pair.phaseAt = now - wait + 2;
                return true;
            }
            if (hostileNear)
                return true;
            // Fighting something else (a mob, an enemy player): it finishes first.
            for (Player* bot : {a, b})
                if (!bot->getAttackers().empty())
                    return true;
            for (Player* bot : {a, b})
            {
                bot->CombatStop(true);
                Refresh(bot);
            }
            // Each one challenges in turn. The other one's AI waits a moment, the challenge is accepted below.
            Player* challenger = (pair.rounds % 2) ? b : a;
            Player* challenged = challenger == a ? b : a;
            if (PlayerbotAI* otherAI = GET_PLAYERBOT_AI(challenged))
                otherAI->SetNextCheckDelay(3000);
            SpellCastResult const result = challenger->CastSpell(challenged, DuelSpell, true);
            if (result != SPELL_CAST_OK && !city.warned)
            {
                city.warned = true;
                LOG_WARN("playerbots",
                         "city duels: {}: the duel spell failed (cast result {}) in area {}; check "
                         "AiPlayerbot.CityDuels.{}Spot",
                         city.name, uint32(result), challenger->GetAreaId(), city.name);
            }
            pair.phase = Phase::Challenged;
            pair.phaseAt = now;
            return true;
        }
        case Phase::Challenged:
        {
            Player* challenger = (pair.rounds % 2) ? b : a;
            Player* challenged = challenger == a ? b : a;
            // Accepted as a click on the button would.
            if (challenged->duel && challenged->duel->State == DUEL_STATE_CHALLENGED &&
                challenged->duel->Opponent == challenger)
            {
                WorldPacket accept(CMSG_DUEL_ACCEPTED, 8);
                accept << challenger->GetGuidValue(PLAYER_DUEL_ARBITER);
                challenged->GetSession()->HandleDuelAcceptedOpcode(accept);
            }
            if (challenger->duel && challenger->duel->State == DUEL_STATE_IN_PROGRESS)
            {
                // Accepting a duel resets a bot's strategies (AcceptDuelAction): held again, free to move.
                Engage(a, b);
                Engage(b, a);
                pair.phase = Phase::Fighting;
                pair.phaseAt = now;
                pair.placed = 0;
                return true;
            }
            if (!challenger->duel || now - pair.phaseAt > ChallengeSeconds)
            {
                if (challenger->duel)
                    challenger->DuelComplete(DUEL_INTERRUPTED);
                if (++pair.failures >= MaxFailures)
                {
                    ReleasePair(city, pair, true, "the duel never started");
                    return false;
                }
                pair.phase = Phase::Resting;
                pair.phaseAt = now;
            }
            return true;
        }
        case Phase::Fighting:
        {
            if (!a->duel)
            {
                ++pair.rounds;
                pair.failures = 0;
                pair.phase = Phase::Resting;
                pair.phaseAt = now;
                return true;
            }
            if (now - pair.phaseAt > DuelMaxSeconds)
            {
                a->DuelComplete(DUEL_INTERRUPTED);
                ++pair.rounds;
                pair.phase = Phase::Resting;
                pair.phaseAt = now;
            }
            return true;
        }
    }
    return true;
}

// A new pair for the first free slot: two free bots of the city's faction, as near the slot's level as there
// are, at most PairLevelGap apart. One search through the random bots.
bool FillPair(City& city, WorldLocation const& spot, Settings const& settings, time_t now)
{
    std::set<uint32> used;
    for (Pair const& pair : city.pairs)
        used.insert(pair.slot);
    uint32 slot = 0;
    while (used.count(slot))
        ++slot;
    uint32 const target = settings.levels[slot % settings.levels.size()];

    std::vector<Player*> free;
    for (auto itr = sRandomPlayerbotMgr.GetPlayerBotsBegin(); itr != sRandomPlayerbotMgr.GetPlayerBotsEnd(); ++itr)
        if (IsFree(itr->second, city.team, settings.minLevel))
            free.push_back(itr->second);
    if (free.size() < 2)
        return false;

    // Random among the bots of the same distance to the level, the nearest first.
    std::vector<std::pair<uint32, Player*>> sorted;
    sorted.reserve(free.size());
    for (Player* bot : free)
        sorted.emplace_back(uint32(std::abs(int32(bot->GetLevel()) - int32(target))) * 1000u + urand(0, 999), bot);
    std::sort(sorted.begin(), sorted.end(),
              [](std::pair<uint32, Player*> const& l, std::pair<uint32, Player*> const& r) { return l.first < r.first; });

    for (size_t i = 0; i < sorted.size(); ++i)
    {
        Player* a = sorted[i].second;
        for (size_t j = i + 1; j < sorted.size(); ++j)
        {
            Player* b = sorted[j].second;
            if (uint32(std::abs(int32(a->GetLevel()) - int32(b->GetLevel()))) > settings.levelGap)
                continue;
            Pair pair;
            pair.slot = slot;
            pair.a = a->GetGUID();
            pair.b = b->GetGUID();
            pair.created = now;
            pair.phaseAt = now;
            Take(a, pair.originA);
            Take(b, pair.originB);
            Place(a, spot, slot, true);
            Place(b, spot, slot, false);
            LOG_DEBUG("playerbots", "city duels: {} slot {}: {} ({}) and {} ({})", city.name, slot, a->GetName(),
                      a->GetLevel(), b->GetName(), b->GetLevel());
            city.pairs.push_back(pair);
            return true;
        }
    }
    return false;
}
}  // namespace

namespace CityDuels
{
bool IsDuellist(ObjectGuid guid)
{
    std::lock_guard<std::mutex> guard(DuellistsLock);
    return Duellists.count(guid) != 0;
}

void ReleaseAll(bool sendBack)
{
    for (City& city : Cities)
    {
        for (Pair const& pair : city.pairs)
            ReleasePair(city, pair, sendBack, "released");
        city.pairs.clear();
        city.nextFill = 0;
    }
    Running = false;
}

void Update(uint32 diff)
{
    Timer += diff;
    if (Timer < Interval)
        return;
    Timer = 0;

    Settings const s = ReadSettings();
    Interval = s.interval;
    if (!s.enable || !s.pairs || !sPlayerbotAIConfig.enabled || !sPlayerbotAIConfig.randomBotAutologin)
    {
        if (Running)
            ReleaseAll(true);
        return;
    }
    Running = true;

    time_t const now = time(nullptr);

    // --- CoA World (CoA Monde session) ---
    // The released bots that could not go back yet.
    for (size_t i = 0; i < PendingBacks.size();)
    {
        PendingBack const& back = PendingBacks[i];
        Player* bot = Find(back.guid);
        bool done = now >= back.until || !bot || IsDuellist(back.guid) || bot->GetGroup() || bot->InBattleground() ||
                    bot->InBattlegroundQueue() || !bot->GetSession() || bot->GetSession()->isLogingOut();
        if (!done && bot->IsInWorld() && bot->IsAlive() && !bot->IsBeingTeleported() && !bot->duel &&
            !bot->IsInCombat() && !bot->IsDuringRemoveFromWorld())
        {
            bot->TeleportTo(back.origin);
            done = true;
        }
        if (done)
            PendingBacks.erase(PendingBacks.begin() + i);
        else
            ++i;
    }
    // --- end CoA World ---

    for (City& city : Cities)
    {
        WorldLocation const spot = CitySpot(city);
        bool const hostileNear = HostilePlayerNear(city, spot);
        for (size_t i = 0; i < city.pairs.size();)
        {
            if (UpdatePair(city, city.pairs[i], spot, s, hostileNear, now))
                ++i;
            else
                city.pairs.erase(city.pairs.begin() + i);
        }
        // Fewer pairs wanted (BotsPerCity lowered): the last ones go at once.
        while (city.pairs.size() > s.pairs)
        {
            ReleasePair(city, city.pairs.back(), true, "fewer pairs wanted");
            city.pairs.pop_back();
        }
        // One new pair per city and update at most: the search goes through every random bot once.
        if (city.pairs.size() < s.pairs && now >= city.nextFill && !hostileNear)
            city.nextFill = FillPair(city, spot, s, now) ? now : now + RetryFillSeconds;
    }
}
}  // namespace CityDuels
