/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaCityLife.h"

#include "CellImpl.h"
#include "CoaCityLifeRules.h"
#include "Creature.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "GameTime.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "RandomPlayerbotMgr.h"

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
};
constexpr uint64 HOLD_MS = 15 * MINUTE * IN_MILLISECONDS;

std::mutex cityLock;
std::unordered_map<uint64, Holding> holding;  // bot -> its station (map threads)

std::function<uint32_t(uint32_t, uint32_t)> const between = [](uint32_t lo, uint32_t hi) { return urand(lo, hi); };

bool Enabled() { return sPlayerbotAIConfig.coaLiveCityEnable; }

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
        return go->GetGoType() == GAMEOBJECT_TYPE_SPELL_FOCUS && origin->IsWithinDistInMap(go, SEARCH_RANGE) &&
               StationOfFocus(go->GetGOInfo()->spellFocus.focusId) != Station::None;
    }
};

Station KindOf(WorldObject* object)
{
    if (Creature* c = object->ToCreature())
        return StationOfCreature(c->GetNpcFlags(), c->GetName());
    if (GameObject* go = object->ToGameObject())
        return go->GetGoType() == GAMEOBJECT_TYPE_SPELL_FOCUS ? StationOfFocus(go->GetGOInfo()->spellFocus.focusId)
                                                               : Station::None;
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
            it = nowMs >= it->second.sinceMs + HOLD_MS ? holding.erase(it) : std::next(it);
    std::unordered_map<ObjectGuid, uint32> held;    // station -> bots holding it (one pass)
    std::unordered_map<ObjectGuid, uint32> begging; // station -> beggars at it
    for (auto const& [bot, h] : holding)
        if (nowMs < h.sinceMs + HOLD_MS)
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
        candidates.push_back({StationOfFocus(go->GetGOInfo()->spellFocus.focusId), go->GetGUID().GetRawValue(),
                              botsAt(go->GetGUID())});
        guids[go->GetGUID().GetRawValue()] = go->GetGUID();
        kinds[go->GetGUID().GetRawValue()] = Station::Forge;
    }
    uint64 const picked = Pick(candidates, between);
    if (!picked)
        return ObjectGuid::Empty;
    // Held from now, so that the next bot sees it fuller.
    Holding& h = holding[bot->GetGUID().GetRawValue()];
    h = Holding();
    h.station = guids[picked];
    h.sinceMs = nowMs;
    h.kind = kinds[picked];
    return h.station;
}

void OnReach(PlayerbotAI* botAI, WorldObject* object)
{
    if (!Enabled() || !botAI || !object)
        return;
    Player* bot = botAI->GetBot();
    Station kind = Station::None;
    {
        std::lock_guard<std::mutex> guard(cityLock);
        auto it = holding.find(bot->GetGUID().GetRawValue());
        if (it == holding.end() || it->second.station != object->GetGUID())
            return;
        if (it->second.kind == Station::None)
            it->second.kind = KindOf(object);
        kind = it->second.kind;
        it->second.stayMs = StayMs(kind, between);
        it->second.lastEmoteMs = GameTime::GetGameTimeMS().count();
    }
    bot->SetFacingToObject(object);
    if (PoseOf(kind).sit)
        bot->SetStandState(UNIT_STAND_STATE_SIT);
    else if (kind == Station::Beg)
        bot->SetStandState(UNIT_STAND_STATE_KNEEL);
}

uint32 WhileStaying(PlayerbotAI* botAI, WorldObject* object, uint32 defaultMs)
{
    if (!Enabled() || !botAI || !object)
        return defaultMs;
    Player* bot = botAI->GetBot();
    uint64 const now = GameTime::GetGameTimeMS().count();
    Pose pose;
    uint32 stay = 0;
    {
        std::lock_guard<std::mutex> guard(cityLock);
        auto it = holding.find(bot->GetGUID().GetRawValue());
        if (it == holding.end() || it->second.station != object->GetGUID() || !it->second.stayMs)
            return defaultMs;
        pose = PoseOf(it->second.kind);
        stay = it->second.stayMs;
        if (!pose.emote || now < it->second.lastEmoteMs + pose.everyMs)
            pose.emote = 0;
        else
            it->second.lastEmoteMs = now;
    }
    if (pose.emote && !bot->IsInCombat())
        bot->HandleEmoteCommand(pose.emote);
    return stay;
}

uint32 VisitMs(Player* bot, uint32 cityStayMs)
{
    return Enabled() && bot && sRandomPlayerbotMgr.IsRandomBot(bot) ? StayMs(bot->GetGUID().GetCounter(), cityStayMs)
                                                                      : cityStayMs;
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
        held = holding.erase(bot->GetGUID().GetRawValue()) > 0;
    }
    if (held && (bot->IsSitState() || bot->getStandState() == UNIT_STAND_STATE_KNEEL))
        bot->SetStandState(UNIT_STAND_STATE_STAND);
}
}  // namespace CoaCityLife
