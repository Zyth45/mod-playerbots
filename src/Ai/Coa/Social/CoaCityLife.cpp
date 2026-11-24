/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaCityLife.h"

#include "CoaSafeMove.h"
#include "CoaBotAuctions.h"
#include "CellImpl.h"
#include "CoaCityLifeRules.h"
#include "CoaLive.h"
#include "CoaPathProbe.h"
#include "Creature.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "GameTime.h"
#include "Group.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "RandomPlayerbotMgr.h"

#include <cmath>
#include <list>
#include <mutex>
#include <unordered_map>

using namespace CoaCityLife;

namespace
{
constexpr float SEARCH_RANGE = 120.0f;  // a capital's district

struct Holding
{
    ObjectGuid station;
    Station kind = Station::None;
    uint32 stayMs = 0;
    uint64 lastEmoteMs = 0;
    uint64 sinceMs = 0;  // a bot that never got there (another activity, a fight) frees it after HOLD_MS
    uint64 untilMs = 0;  // held until then: sinceMs + HOLD_MS, longer for a long post once reached
    bool inRoom = false;  // stepped from its auctioneer into the room (IntoRoom)
    bool roomTried = false;  // 3.2: the step was tried (it waits for the auction visit)
    // 3.2.2: a square: which one, the bot's slot on it and the square's middle (it faces it), sitting or not.
    uint32 placeId = 0;
    float px = 0.0f, py = 0.0f, pz = 0.0f, cx = 0.0f, cy = 0.0f;
    bool sitting = false;
    int8 seat = 0;  // 3.2.2 (measure only): a seat's use at arrival, 1 used (Use), -1 refused (SeatUsable)
    uint8 satAgain = 0;      // 3.2.2: times the bot, found standing on its seat, sat on it again (2 at most)
    uint64 satAgainMs = 0;
};
constexpr uint64 HOLD_MS = 15 * MINUTE * IN_MILLISECONDS;
constexpr uint64 SAT_AGAIN_MS = 10 * IN_MILLISECONDS;  // 3.2.2: a seat holder found standing sits again after
constexpr uint64 POST_MARGIN_MS = 5 * MINUTE * IN_MILLISECONDS;  // a long post is held this much past its end

std::mutex cityLock;
std::unordered_map<uint64, Holding> holding;  // bot -> its station (map threads)
std::unordered_map<uint64, Station> lastKind;  // bot -> the kind of its last stop, not taken twice in a row

uint32 FocusOf(GameObject const* go)
{
    return go->GetGoType() == GAMEOBJECT_TYPE_SPELL_FOCUS ? go->GetGOInfo()->spellFocus.focusId : 0;
}

std::function<uint32_t(uint32_t, uint32_t)> const between = [](uint32_t lo, uint32_t hi) { return urand(lo, hi); };

bool Enabled() { return sPlayerbotAIConfig.coaLiveCityEnable; }

struct AuctioneerCheck
{
    WorldObject const* from;
    bool operator()(Creature* c) const
    {
        return c->IsAlive() && c->HasNpcFlag(UNIT_NPC_FLAG_AUCTIONEER) && from->IsWithinDist(c, 40.0f);
    }
};

// After a word with its auctioneer, a step into the room (IntoRoom): true when the bot moves.
bool StepIntoRoom(Player* bot, WorldObject* auctioneer)
{
    std::list<Creature*> auctioneers;
    AuctioneerCheck check{auctioneer};
    Acore::CreatureListSearcher<AuctioneerCheck> searcher(auctioneer, auctioneers, check);
    Cell::VisitObjects(auctioneer, searcher, 40.0f);
    if (auctioneers.size() < 2)
        return false;
    float cx = 0.0f, cy = 0.0f;
    for (Creature* c : auctioneers)
    {
        cx += c->GetPositionX();
        cy += c->GetPositionY();
    }
    cx /= auctioneers.size();
    cy /= auctioneers.size();
    float const step = ROOM_STEP_MIN + float(urand(0, 100)) / 100.0f * (ROOM_STEP_MAX - ROOM_STEP_MIN);
    float const side = (float(urand(0, 200)) / 100.0f - 1.0f) * ROOM_SIDE;
    float x = 0.0f, y = 0.0f;
    if (!IntoRoom(auctioneer->GetPositionX(), auctioneer->GetPositionY(), cx, cy, step, side, x, y))
        return false;
    float const z = bot->GetMap()->GetHeight(bot->GetPhaseMask(), x, y, auctioneer->GetPositionZ() + 2.0f);
    if (z <= INVALID_HEIGHT || std::fabs(z - auctioneer->GetPositionZ()) > 3.0f ||
        !auctioneer->IsWithinLOS(x, y, z + 1.5f))
        return false;
    return CoaSafeMove::MovePoint(bot, x, y, z);
}

bool InCapital(Player* bot)
{
    AreaTableEntry const* zone = sAreaTableStore.LookupEntry(bot->GetZoneId());
    return zone && (zone->flags & AREA_FLAG_CAPITAL);
}

struct StationCreatureCheck
{
    WorldObject const* origin;
    bool operator()(Creature* c) const
    {
        return c->IsAlive() && origin->IsWithinDistInMap(c, SEARCH_RANGE) &&
               StationOfCreature(c->GetNpcFlags(), c->GetName()) != Station::None;
    }
};

struct StationObjectCheck
{
    WorldObject const* origin;
    bool operator()(GameObject* go) const
    {
        Station const kind = go->isSpawned() ? StationOfObject(go->GetGoType(), FocusOf(go)) : Station::None;
        // A seat only on the bot's own floor: never a chair upstairs, on a balcony or on a roof.
        return kind != Station::None &&
               (kind != Station::Seat || SeatPickable(origin->GetPositionZ(), go->GetPositionZ())) &&
               origin->IsWithinDistInMap(go, SEARCH_RANGE);
    }
};

Station KindOf(WorldObject* object)
{
    if (Creature* c = object->ToCreature())
        return StationOfCreature(c->GetNpcFlags(), c->GetName());
    if (GameObject* go = object->ToGameObject())
        return StationOfObject(go->GetGoType(), FocusOf(go));
    return Station::None;
}
}  // namespace

namespace CoaCityLife
{
ObjectGuid ChooseStation(PlayerbotAI* botAI)
{
    if (!Enabled() || !botAI)
        return ObjectGuid::Empty;
    Player* bot = botAI->GetBot();
    // A random bot of no player, on a city visit, now and then.
    if (!bot || !sRandomPlayerbotMgr.IsRandomBot(bot) || botAI->HasGameClientMaster() || !botAI->rpgInfo.cityStayMs ||
        !InCapital(bot) || urand(1, 100) > CHANCE_PCT)
        return ObjectGuid::Empty;

    // A stop at the water of the city instead (3.1.6, AiPlayerbot.CoaLive.City.FishingPct): the CoA Live
    // fishing, with its cap. The visit ends with it.
    if (sPlayerbotAIConfig.coaLiveCityFishingPct && urand(1, 100) <= sPlayerbotAIConfig.coaLiveCityFishingPct &&
        CoaLive::FishInCity(bot, botAI))
        return ObjectGuid::Empty;

    std::list<Creature*> creatures;
    StationCreatureCheck creatureCheck{bot};
    Acore::CreatureListSearcher<StationCreatureCheck> creatureSearcher(bot, creatures, creatureCheck);
    Cell::VisitObjects(bot, creatureSearcher, SEARCH_RANGE);
    std::list<GameObject*> objects;
    StationObjectCheck objectCheck{bot};
    Acore::GameObjectListSearcher<StationObjectCheck> objectSearcher(bot, objects, objectCheck);
    Cell::VisitObjects(bot, objectSearcher, SEARCH_RANGE);

    uint64 const nowMs = GameTime::GetGameTimeMS().count();
    std::lock_guard<std::mutex> guard(cityLock);
    if (holding.size() > 4096)
        for (auto it = holding.begin(); it != holding.end();)
            it = nowMs >= it->second.untilMs ? holding.erase(it) : std::next(it);
    if (lastKind.size() > 20000)
        lastKind.clear();
    std::unordered_map<ObjectGuid, uint32> held;    // station -> bots holding it (one pass)
    std::unordered_map<ObjectGuid, uint32> begging; // station -> beggars at it
    for (auto const& [bot, h] : holding)
        if (nowMs < h.untilMs)
            ++(h.kind == Station::Beg ? begging : held)[h.station];
    auto botsAt = [&held](ObjectGuid station)
    {
        auto it = held.find(station);
        return it == held.end() ? 0u : it->second;
    };
    auto beggarsAt = [&begging](ObjectGuid station)
    {
        auto it = begging.find(station);
        return it == begging.end() ? 0u : it->second;
    };
    std::unordered_map<uint64, Station> kinds;  // the candidate picked -> what the bot does there
    std::vector<Candidate> candidates;
    std::unordered_map<uint64, ObjectGuid> guids;
    // 3.2.2: the squares around (CoaPathProbe), each anchored on the station creature nearest it; never a full one.
    constexpr uint64 SQUARE_KEY = 1ull << 62;
    std::unordered_map<uint64, CoaPathProbe::Place> squares;
    if (sPlayerbotAIConfig.coaLiveCitySquares && !creatures.empty())
    {
        std::unordered_map<uint32, uint32> onSquare;  // place id -> bots holding it
        for (auto const& [holder, h] : holding)
            if (nowMs < h.untilMs && h.kind == Station::Square)
                ++onSquare[h.placeId];
        for (CoaPathProbe::Place const& place :
             CoaPathProbe::PlacesNear(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), SEARCH_RANGE))
        {
            uint32 const there = onSquare.count(place.id) ? onSquare[place.id] : 0;
            if (there >= std::min<uint32>(sPlayerbotAIConfig.coaLiveCitySquareMaxBots, MaxBots(Station::Square)))
                continue;
            Creature* anchor = nullptr;
            for (Creature* c : creatures)
                if (!anchor || c->GetDistance2d(place.x, place.y) < anchor->GetDistance2d(place.x, place.y))
                    anchor = c;
            uint64 const key = SQUARE_KEY | place.id;
            candidates.push_back({Station::Square, key, there});
            guids[key] = anchor->GetGUID();
            kinds[key] = Station::Square;
            squares[key] = place;
        }
    }
    // A beggar's candidate shares its banker's or auctioneer's guid: its key is the guid with the top bit set.
    constexpr uint64 BEG_KEY = 1ull << 63;
    for (Creature* c : creatures)
    {
        Station const kind = StationOfCreature(c->GetNpcFlags(), c->GetName());
        candidates.push_back({kind, c->GetGUID().GetRawValue(), botsAt(c->GetGUID())});
        guids[c->GetGUID().GetRawValue()] = c->GetGUID();
        kinds[c->GetGUID().GetRawValue()] = kind;
        if (kind == Station::Bank || kind == Station::Auction)
        {
            candidates.push_back({Station::Beg, c->GetGUID().GetRawValue() | BEG_KEY, beggarsAt(c->GetGUID())});
            guids[c->GetGUID().GetRawValue() | BEG_KEY] = c->GetGUID();
            kinds[c->GetGUID().GetRawValue() | BEG_KEY] = Station::Beg;
        }
    }
    for (GameObject* go : objects)
    {
        Station const kind = StationOfObject(go->GetGoType(), FocusOf(go));
        candidates.push_back({kind, go->GetGUID().GetRawValue(), botsAt(go->GetGUID())});
        guids[go->GetGUID().GetRawValue()] = go->GetGUID();
        kinds[go->GetGUID().GetRawValue()] = kind;
    }
    auto const last = lastKind.find(bot->GetGUID().GetRawValue());
    uint64 const picked = Pick(candidates, between, last == lastKind.end() ? Station::None : last->second);
    if (!picked)
        return ObjectGuid::Empty;
    // Held from now, so that the next bot sees it fuller.
    Holding& h = holding[bot->GetGUID().GetRawValue()];
    h = Holding();
    h.station = guids[picked];
    h.sinceMs = nowMs;
    h.untilMs = nowMs + HOLD_MS;
    h.kind = kinds[picked];
    if (h.kind == Station::Square)
    {
        CoaPathProbe::Place const& place = squares[picked];
        uint32 slot = 0;  // the first free slot of the square
        for (bool taken = true; taken && slot < 3; slot += taken ? 1 : 0)
        {
            taken = false;
            for (auto const& [holder, other] : holding)
                if (holder != bot->GetGUID().GetRawValue() && other.kind == Station::Square &&
                    other.placeId == place.id && nowMs < other.untilMs)
                {
                    float ox = 0.0f, oy = 0.0f;
                    PlaceSlot(slot, place.x, place.y, ox, oy);
                    taken |= std::hypot(other.px - ox, other.py - oy) < 0.1f;
                }
        }
        h.placeId = place.id;
        PlaceSlot(slot, place.x, place.y, h.px, h.py);
        h.pz = place.z;
        h.cx = place.x;
        h.cy = place.y;
    }
    return h.station;
}

// 3.2.2 (measure only, the user: bots standing on chairs): how the bot's seat was taken, for the probe's hourly line.
static void NoteSeatUse(Player* bot, int8 used)
{
    CoaPathProbe::NoteSeatUse(used > 0);
    std::lock_guard<std::mutex> guard(cityLock);
    auto it = holding.find(bot->GetGUID().GetRawValue());
    if (it != holding.end())
        it->second.seat = used;
}

void OnReach(PlayerbotAI* botAI, WorldObject* object)
{
    if (!Enabled() || !botAI || !object)
        return;
    Player* bot = botAI->GetBot();
    Station kind = Station::None;
    uint32 stayMs = 0;
    {
        std::lock_guard<std::mutex> guard(cityLock);
        auto it = holding.find(bot->GetGUID().GetRawValue());
        if (it == holding.end() || it->second.station != object->GetGUID())
            return;
        if (it->second.kind == Station::None)
            it->second.kind = KindOf(object);
        kind = it->second.kind;
        stayMs = StayOrPostMs(kind, sPlayerbotAIConfig.coaLiveCityPostChancePct,
                              sPlayerbotAIConfig.coaLiveCityPostMinMinutes,
                              sPlayerbotAIConfig.coaLiveCityPostMaxMinutes, between);
        it->second.stayMs = stayMs;
        uint64 const nowMs = GameTime::GetGameTimeMS().count();
        it->second.lastEmoteMs = nowMs;
        it->second.untilMs = std::max<uint64>(it->second.untilMs, nowMs + stayMs + POST_MARGIN_MS);
    }
    // A long post outlasts the city visit: the visit waits for it.
    NewRpgInfo& info = botAI->rpgInfo;
    if (info.cityStayMs)
        info.cityStayMs =
            std::max<uint32>(info.cityStayMs, GetMSTimeDiffToNow(info.startT) + stayMs + MINUTE * IN_MILLISECONDS);
    if (kind == Station::Square)
    {
        // 3.2.2: facing the square's middle (the others), sitting or talking.
        float cx = 0.0f, cy = 0.0f;
        bool sit = urand(1, 100) <= PLACE_SIT_PCT;
        {
            std::lock_guard<std::mutex> guard(cityLock);
            auto it = holding.find(bot->GetGUID().GetRawValue());
            if (it == holding.end())
                return;
            cx = it->second.cx;
            cy = it->second.cy;
            it->second.sitting = sit;
        }
        bot->SetFacingTo(bot->GetAngle(cx, cy));
        if (sit)
            bot->SetStandState(UNIT_STAND_STATE_SIT);
        CoaPathProbe::NotePlaceTaken();
        return;
    }
    if (kind == Station::Seat)
    {
        // Sat on the seat itself, the way a player clicks a chair. Using a chair puts the bot on it wherever it
        // stands: only next to it, on its floor and in sight, else the bot would be lifted onto a roof or a balcony.
        GameObject* seat = object->ToGameObject();
        if (seat && SeatUsable(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), seat->GetPositionX(),
                               seat->GetPositionY(), seat->GetPositionZ(), bot->IsWithinLOSInMap(seat)))
        {
            seat->Use(bot);
            NoteSeatUse(bot, 1);
            return;
        }
        if (seat)
        {
            bot->SetFacingToObject(object);  // out of reach: stands by it, never lifted onto it
            NoteSeatUse(bot, -1);
            return;
        }
    }
    bot->SetFacingToObject(object);
    // 3.2: never while its auction visit waits for the world thread: the core's handlers refuse a bot that has walked
    // off, and every City Life stop at an auctioneer ended in a visit that did nothing. WhileStaying steps later.
    if (kind == Station::Auction && !CoaEconomy::BotAuctions::HasPendingVisit(bot->GetGUID()))
    {
        bool const stepped = StepIntoRoom(bot, object);
        std::lock_guard<std::mutex> guard(cityLock);
        auto it = holding.find(bot->GetGUID().GetRawValue());
        if (it != holding.end())
        {
            it->second.roomTried = true;
            it->second.inRoom = stepped;
        }
    }
    if (PoseOf(kind).sit)
        bot->SetStandState(UNIT_STAND_STATE_SIT);
    else if (kind == Station::Beg)
        bot->SetStandState(UNIT_STAND_STATE_KNEEL);
}

bool KeepsSeated(PlayerbotAI* botAI)
{
    if (!Enabled() || !botAI || !botAI->GetBot()->IsSitState())
        return false;
    // Never for a bot a player deals with: in his group, or trading with him.
    Player* bot = botAI->GetBot();
    if (bot->GetTradeData())
        return false;
    if (Group* group = bot->GetGroup())
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            if (Player* member = ref->GetSource())
                if (member != bot && member->GetSession() && !member->GetSession()->IsBot())
                    return false;
    std::lock_guard<std::mutex> guard(cityLock);
    auto const it = holding.find(botAI->GetBot()->GetGUID().GetRawValue());
    return it != holding.end() && (it->second.kind == Station::Seat || it->second.sitting);
}

uint32 WhileStaying(PlayerbotAI* botAI, WorldObject* object, uint32 defaultMs)
{
    if (!Enabled() || !botAI || !object)
        return defaultMs;
    Player* bot = botAI->GetBot();
    uint64 const now = GameTime::GetGameTimeMS().count();
    Pose pose;
    uint32 stay = 0;
    bool stepNow = false;
    bool seatHeld = false;
    bool sitAgain = false;
    int8 seatUse = 0;
    {
        std::lock_guard<std::mutex> guard(cityLock);
        auto it = holding.find(bot->GetGUID().GetRawValue());
        if (it == holding.end() || it->second.station != object->GetGUID() || !it->second.stayMs)
            return defaultMs;
        // 3.2: the step into the room, once its auction visit has run.
        if (it->second.kind == Station::Auction && !it->second.roomTried)
            stepNow = it->second.roomTried = true;
        pose = PoseOf(it->second.kind);
        if (it->second.kind == Station::Seat)
            seatUse = it->second.seat;
        seatHeld = it->second.kind == Station::Seat;
        // 3.2.2 (bench of 3606e5be): a bot that took its seat, found standing on it (still, out of a fight), sits on
        // it again, at most every SAT_AGAIN_MS and twice a stay: a seat taken by another, or a Use that fails, is
        // given up (the bot stands by it).
        if (seatHeld && it->second.seat > 0 && !bot->IsSitState() && !bot->isMoving() && !bot->IsInCombat() &&
            now >= it->second.satAgainMs + SAT_AGAIN_MS)
        {
            if (it->second.satAgain >= 2)
                it->second.seat = -2;  // given up
            else
            {
                ++it->second.satAgain;
                it->second.satAgainMs = now;
                sitAgain = true;
            }
        }
        if (it->second.sitting)
            pose.emote = 0;  // 3.2.2: sitting on a square, no talk gestures
        stay = it->second.stayMs;
        if (!pose.emote || now < it->second.lastEmoteMs + pose.everyMs)
            pose.emote = 0;
        else
            it->second.lastEmoteMs = now;
    }
    if (stepNow)
    {
        if (CoaEconomy::BotAuctions::HasPendingVisit(bot->GetGUID()))
        {
            std::lock_guard<std::mutex> guard(cityLock);
            auto it = holding.find(bot->GetGUID().GetRawValue());
            if (it != holding.end())
                it->second.roomTried = false;  // still waiting: next update
        }
        else if (StepIntoRoom(bot, object))
        {
            std::lock_guard<std::mutex> guard(cityLock);
            auto it = holding.find(bot->GetGUID().GetRawValue());
            if (it != holding.end())
                it->second.inRoom = true;
        }
    }
    if (pose.emote && !bot->IsInCombat())
        bot->HandleEmoteCommand(pose.emote);
    if (sitAgain)
        if (GameObject* seat = object->ToGameObject())
        {
            seat->Use(bot);
            CoaPathProbe::NoteSeatSatAgain();
        }
    if (seatHeld)
        CoaPathProbe::NoteSeatHeld(bot, object, seatUse);
    return stay;
}

uint32 VisitMs(Player* bot, uint32 cityStayMs)
{
    return Enabled() && bot && sRandomPlayerbotMgr.IsRandomBot(bot) ? StayMs(bot->GetGUID().GetCounter(), cityStayMs)
                                                                      : cityStayMs;
}

bool InRoom(PlayerbotAI* botAI, WorldObject* object)
{
    if (!Enabled() || !botAI || !object)
        return false;
    Player* bot = botAI->GetBot();
    {
        std::lock_guard<std::mutex> guard(cityLock);
        auto it = holding.find(bot->GetGUID().GetRawValue());
        if (it == holding.end() || it->second.station != object->GetGUID() || !it->second.inRoom)
            return false;
    }
    return bot->IsWithinDist(object, ROOM_KEEP);
}

bool HoldsSquare(PlayerbotAI* botAI, ObjectGuid station)
{
    if (!Enabled() || !botAI || station.IsEmpty())
        return false;
    std::lock_guard<std::mutex> guard(cityLock);
    auto const it = holding.find(botAI->GetBot()->GetGUID().GetRawValue());
    return it != holding.end() && it->second.station == station && it->second.kind == Station::Square;
}

bool SquareSpot(PlayerbotAI* botAI, ObjectGuid station, float& x, float& y, float& z)
{
    if (!Enabled() || !botAI || station.IsEmpty())
        return false;
    std::lock_guard<std::mutex> guard(cityLock);
    auto const it = holding.find(botAI->GetBot()->GetGUID().GetRawValue());
    if (it == holding.end() || it->second.station != station || it->second.kind != Station::Square)
        return false;
    x = it->second.px;
    y = it->second.py;
    z = it->second.pz;
    return true;
}

bool AtSquare(PlayerbotAI* botAI)
{
    if (!Enabled() || !botAI)
        return false;
    Player* bot = botAI->GetBot();
    std::lock_guard<std::mutex> guard(cityLock);
    auto const it = holding.find(bot->GetGUID().GetRawValue());
    return it != holding.end() && it->second.kind == Station::Square &&
           std::hypot(bot->GetPositionX() - it->second.px, bot->GetPositionY() - it->second.py) <= PLACE_ARRIVED &&
           std::fabs(bot->GetPositionZ() - it->second.pz) <= 3.0f;
}

bool Holds(PlayerbotAI* botAI, ObjectGuid station)
{
    if (!Enabled() || !botAI || station.IsEmpty())
        return false;
    std::lock_guard<std::mutex> guard(cityLock);
    auto const it = holding.find(botAI->GetBot()->GetGUID().GetRawValue());
    return it != holding.end() && it->second.station == station;
}

void Leave(PlayerbotAI* botAI)
{
    // Off: no lock on every NPC left. A station held when it was turned off frees itself after HOLD_MS.
    if (!Enabled() || !botAI)
        return;
    Player* bot = botAI->GetBot();
    bool held = false;
    {
        std::lock_guard<std::mutex> guard(cityLock);
        auto const it = holding.find(bot->GetGUID().GetRawValue());
        if (it != holding.end())
        {
            held = true;
            if (it->second.kind != Station::None)
                lastKind[it->first] = it->second.kind;
            holding.erase(it);
        }
    }
    if (held && (bot->IsSitState() || bot->getStandState() == UNIT_STAND_STATE_KNEEL))
        bot->SetStandState(UNIT_STAND_STATE_STAND);
}
}  // namespace CoaCityLife
