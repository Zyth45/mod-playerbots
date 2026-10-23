/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "OtherSideShared.h"

#include "Creature.h"
#include "Group.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Spell.h"
#include "Timer.h"
#include "Unit.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace OtherSideShared
{
namespace
{
constexpr float SEARCH_RANGE = 60.0f;
constexpr float MARGIN = 2.5f;

// Radii as the core's scripts use them, plus MARGIN.
constexpr float MUZAH_SWIRL_RADIUS = 3.0f + MARGIN;     // BatCrashRadius
constexpr float MUZAH_GLAIVE_RADIUS = 2.0f + MARGIN;    // Ripped by the Glaives, 2 yd
constexpr float MUZAH_WEB_RADIUS = 8.0f + 3.0f;         // Entangling Web, 8 yd around Muzah, 4 s cast
constexpr float GLAIVE_STRIKE_HALF_WIDTH = 3.0f + MARGIN;
constexpr float PILLAR_RADIUS = 5.0f + MARGIN;          // Necrotic Emerge when a pillar bursts
constexpr float MISSILE_RADIUS = 5.0f + MARGIN;         // Death Missile
constexpr float ORB_RADIUS = 2.0f + MARGIN;             // OrbTouch
constexpr float BEAM_LENGTH = 45.0f + 2.0f;
constexpr float BEAM_MIN_HALF_WIDTH = 2.0f + MARGIN;
constexpr float SPIKE_RADIUS = 4.0f + MARGIN;           // Spike, 0.5 yd at a radius mod of 800 %
constexpr float BARRIER_RADIUS = 10.0f + 1.0f;          // eggs land from 11.5 yd on, so no full MARGIN
constexpr float POISON_SWIRL_RADIUS = 6.5f + MARGIN;    // the blob reaches 6.5 yd ahead of its swirl
constexpr float WEB_HALF_WIDTH = 1.5f + MARGIN;         // WebTouch
constexpr float LINE_LENGTH = 60.0f;

constexpr float DEG = float(M_PI) / 180.0f;

constexpr uint32 CACHE_MS = 100;

float Signed(float angle)
{
    angle = Position::NormalizeOrientation(angle);
    return angle > float(M_PI) ? angle - 2.0f * float(M_PI) : angle;
}

bool Casting(Unit* unit, std::initializer_list<uint32> spells)
{
    for (uint32 spell : spells)
        if (unit->FindCurrentSpellBySpellId(spell))
            return true;
    return false;
}

void AddStrip(Hazards& hazards, WorldObject const* from, float length, float halfWidth)
{
    hazards.strips.push_back(
        {from->GetPositionX(), from->GetPositionY(), from->GetOrientation(), length, halfWidth});
}

void AddCircle(Hazards& hazards, WorldObject const* at, float radius)
{
    hazards.circles.push_back({at->GetPositionX(), at->GetPositionY(), radius});
}

void AddBeam(Player* bot, Hazards& hazards, Creature* drakmath)
{
    if (!Casting(drakmath, {SPELL_DEATH_BEAM}))
        return;
    Creature* walker = ObjectAccessor::GetCreature(*bot, drakmath->GetTarget());
    if (!walker)
        return;

    float const angle = drakmath->GetAngle(walker);
    // The walker runs one round of its path while the beam is channeled: whichever way it turns,
    // the ground the beam has just left is the safe side.
    float back = 20.0f * DEG;
    float ahead = 20.0f * DEG;
    if (walker->isMoving())
    {
        float const turn = std::sin(walker->GetOrientation() - angle);
        float const lead = 50.0f * DEG;
        if (turn >= 0.0f)
            ahead = lead;
        else
            back = lead;
    }
    hazards.sweeps.push_back({drakmath->GetPositionX(), drakmath->GetPositionY(), angle, BEAM_LENGTH,
                              BEAM_MIN_HALF_WIDTH, back, ahead});
}
}

bool Hazards::Contains(float x, float y) const
{
    for (Circle const& circle : circles)
    {
        float const dx = x - circle.x;
        float const dy = y - circle.y;
        if (dx * dx + dy * dy <= circle.radius * circle.radius)
            return true;
    }

    for (Strip const& strip : strips)
    {
        float const dx = x - strip.x;
        float const dy = y - strip.y;
        float const along = dx * std::cos(strip.angle) + dy * std::sin(strip.angle);
        float const across = std::fabs(-dx * std::sin(strip.angle) + dy * std::cos(strip.angle));
        if (along >= -strip.halfWidth && along <= strip.length && across <= strip.halfWidth)
            return true;
    }

    for (Sweep const& sweep : sweeps)
    {
        float const dx = x - sweep.x;
        float const dy = y - sweep.y;
        float const dist = std::sqrt(dx * dx + dy * dy);
        if (dist > sweep.length)
            continue;
        float const off = Signed(std::atan2(dy, dx) - sweep.angle);
        if (off >= -sweep.back && off <= sweep.ahead)
            return true;
        if (dist * std::cos(off) > 0.0f && std::fabs(dist * std::sin(off)) <= sweep.minHalfWidth)
            return true;
    }

    return false;
}

Hazards CollectHazards(Player* bot)
{
    Hazards hazards;

    std::list<Creature*> creatures;
    bot->GetCreatureListWithEntryInGrid(creatures,
        std::vector<uint32>{NPC_MUZAH, NPC_DRAKMATH, NPC_MUZAH_GLAIVE, NPC_MUZAH_SWIRL, NPC_DRAKMATH_PILLAR,
                            NPC_DRAKMATH_ORB, NPC_EMBRYO_POISON, NPC_EMBRYO_WEB_MARK, NPC_EMBRYO_WEB,
                            NPC_EMBRYO_SPIKE},
        SEARCH_RANGE);

    for (Creature* creature : creatures)
    {
        if (!creature->IsAlive())
            continue;

        switch (creature->GetEntry())
        {
            case NPC_MUZAH:
                if (creature->IsInCombat() &&
                    Casting(creature, {SPELL_ENTANGLING_WEB, SPELL_ENTANGLING_WEB_HC, SPELL_ENTANGLING_WEB_MYTHIC}))
                    AddCircle(hazards, creature, MUZAH_WEB_RADIUS);
                break;
            case NPC_DRAKMATH:
                if (creature->IsInCombat())
                    AddBeam(bot, hazards, creature);
                break;
            case NPC_MUZAH_GLAIVE:
                AddCircle(hazards, creature, MUZAH_GLAIVE_RADIUS);
                if (creature->isMoving())
                    AddStrip(hazards, creature, 4.0f, MUZAH_GLAIVE_RADIUS);
                break;
            case NPC_MUZAH_SWIRL:
                if (creature->HasAura(SPELL_GROUND_ZONE))
                    AddCircle(hazards, creature, MUZAH_SWIRL_RADIUS);
                else if (Casting(creature, {SPELL_GLAIVE_STRIKE}))
                    AddStrip(hazards, creature, LINE_LENGTH, GLAIVE_STRIKE_HALF_WIDTH);
                break;
            case NPC_DRAKMATH_PILLAR:
                if (creature->HasAura(SPELL_PILLAR))
                    AddCircle(hazards, creature, PILLAR_RADIUS);
                else if (creature->HasAura(SPELL_IMPACT_ZONE))
                    AddCircle(hazards, creature, MISSILE_RADIUS);
                break;
            case NPC_DRAKMATH_ORB:
                AddCircle(hazards, creature, ORB_RADIUS);
                AddStrip(hazards, creature, 5.0f, ORB_RADIUS);
                break;
            case NPC_EMBRYO_POISON:
                if (creature->HasAura(SPELL_POISON_CIRCLE))
                    AddCircle(hazards, creature, BARRIER_RADIUS);
                else if (creature->HasAura(SPELL_POISON_SWIRL))
                {
                    AddCircle(hazards, creature, POISON_SWIRL_RADIUS);
                    // 5 yd/s on its orbit: where it will be in two seconds is already no place to stand.
                    AddStrip(hazards, creature, 10.0f, POISON_SWIRL_RADIUS);
                }
                break;
            case NPC_EMBRYO_WEB_MARK:
                // While it still turns after its target, that one can't outrun it; the others step aside.
                if (creature->GetTarget() != bot->GetGUID())
                    AddStrip(hazards, creature, LINE_LENGTH, WEB_HALF_WIDTH);
                break;
            case NPC_EMBRYO_WEB:
                if (creature->HasAura(SPELL_WEB_ACTIVE))
                    AddStrip(hazards, creature, LINE_LENGTH, WEB_HALF_WIDTH);
                break;
            case NPC_EMBRYO_SPIKE:
                if (creature->HasAura(SPELL_GROUND_ZONE))
                    AddCircle(hazards, creature, SPIKE_RADIUS);
                break;
            default:
                break;
        }
    }

    return hazards;
}

bool FindSafeSpot(Player* bot, Hazards const& hazards, Position& spot, Position const* toward)
{
    static float const rings[] = {2.5f, 4.0f, 6.0f, 8.0f, 11.0f, 14.0f, 18.0f, 22.0f};
    constexpr uint32 directions = 16;

    Unit* target = bot->GetVictim();
    float const keep = target ? bot->GetExactDist2d(target) : 0.0f;

    for (float ring : rings)
    {
        bool found = false;
        float bestScore = 0.0f;
        for (uint32 i = 0; i < directions; ++i)
        {
            float const angle = 2.0f * float(M_PI) * float(i) / float(directions);
            float x = bot->GetPositionX() + ring * std::cos(angle);
            float y = bot->GetPositionY() + ring * std::sin(angle);
            float z = bot->GetPositionZ();
            if (!bot->GetMap()->CheckCollisionAndGetValidCoords(bot, bot->GetPositionX(), bot->GetPositionY(),
                                                                bot->GetPositionZ(), x, y, z))
                continue;
            if (hazards.Contains(x, y))
                continue;

            float score = 0.0f;
            if (toward)
                score = toward->GetExactDist2d(x, y);
            else if (target)
                score = std::fabs(target->GetExactDist2d(x, y) - keep);
            if (!found || score < bestScore)
            {
                found = true;
                bestScore = score;
                spot.Relocate(x, y, z);
            }
        }
        if (found)
            return true;
    }

    return false;
}

namespace
{
constexpr float EGG_LOOKAHEAD = 70.0f * DEG;
constexpr float RUN_STEP = 30.0f * DEG;
constexpr float RUN_MIN_RADIUS = 12.0f;
constexpr float RUN_WALL_GAP = 2.5f;

// How far `to` lies ahead of `from` going counterclockwise around `center`: 0 to 2 pi.
float AheadGap(Position const& center, WorldObject const* from, WorldObject const* to)
{
    return Position::NormalizeOrientation(center.GetAngle(to) - center.GetAngle(from));
}
}

bool EggPhaseCenter(Player* bot, Position& center)
{
    std::list<Creature*> zones;
    bot->GetCreatureListWithEntryInGrid(zones, NPC_EMBRYO_POISON, SEARCH_RANGE);
    for (Creature* zone : zones)
        if (zone->IsAlive() && zone->HasAura(SPELL_POISON_CIRCLE))
        {
            center.Relocate(zone);
            return true;
        }
    return false;
}

Position NextRunSpot(Player* bot, Position const& center)
{
    float const angle = center.GetAngle(bot) + RUN_STEP;
    float wx = center.GetPositionX() + 40.0f * std::cos(angle);
    float wy = center.GetPositionY() + 40.0f * std::sin(angle);
    float wz = center.GetPositionZ();
    bot->GetMap()->CheckCollisionAndGetValidCoords(bot, center.GetPositionX(), center.GetPositionY(),
                                                   center.GetPositionZ(), wx, wy, wz);
    float const radius = std::max(RUN_MIN_RADIUS, center.GetExactDist2d(wx, wy) - RUN_WALL_GAP);

    float x = center.GetPositionX() + radius * std::cos(angle);
    float y = center.GetPositionY() + radius * std::sin(angle);
    float z = bot->GetPositionZ();
    bot->GetMap()->CheckCollisionAndGetValidCoords(bot, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                                                   x, y, z);
    return Position(x, y, z);
}

Creature* EggToKick(Player* bot)
{
    Position center;
    if (!EggPhaseCenter(bot, center))
        return nullptr;

    std::list<Creature*> found;
    bot->GetCreatureListWithEntryInGrid(found, NPC_EMBRYO_EGG, SEARCH_RANGE);
    std::vector<Creature*> eggs;
    for (Creature* egg : found)
        if (egg->IsAlive() && !egg->isMoving() && !egg->HasUnitState(UNIT_STATE_JUMPING))
            eggs.push_back(egg);
    if (eggs.empty())
        return nullptr;
    std::sort(eggs.begin(), eggs.end(),
              [](Creature* a, Creature* b) { return a->GetGUID().GetCounter() < b->GetGUID().GetCounter(); });

    std::vector<Player*> runners;
    if (Group* group = bot->GetGroup())
    {
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (member && member->IsAlive() && member->IsInMap(bot))
                runners.push_back(member);
        }
    }
    else
        runners.push_back(bot);

    // Each egg to the member just behind it, so nobody stops or turns round for one.
    for (Creature* egg : eggs)
    {
        Player* first = nullptr;
        float firstGap = EGG_LOOKAHEAD;
        for (Player* runner : runners)
        {
            float const gap = AheadGap(center, runner, egg);
            if (gap <= firstGap)
            {
                first = runner;
                firstGap = gap;
            }
        }
        if (!first)
            continue;
        if (first == bot)
            return egg;
        runners.erase(std::find(runners.begin(), runners.end(), first));
    }

    return nullptr;
}

Unit* NearestSpider(Player* bot)
{
    std::list<Creature*> spiders;
    bot->GetCreatureListWithEntryInGrid(spiders, NPC_EMBRYO_SPIDER, 40.0f);
    Unit* nearest = nullptr;
    for (Creature* spider : spiders)
        if (spider->IsAlive() && spider->IsInCombat() && bot->IsValidAttackTarget(spider) &&
            (!nearest || bot->GetExactDist2d(spider) < bot->GetExactDist2d(nearest)))
            nearest = spider;
    return nearest;
}

namespace
{
struct Cached
{
    uint32 readAt = 0;
    bool inHazard = false;
    bool eggPhase = false;
    ObjectGuid egg;
};

// Bots of different maps update on different threads.
Cached& Refresh(Player* bot)
{
    thread_local std::unordered_map<ObjectGuid, Cached> cache;
    Cached& entry = cache[bot->GetGUID()];
    uint32 const now = getMSTime();
    if (entry.readAt && getMSTimeDiff(entry.readAt, now) < CACHE_MS)
        return entry;

    entry.readAt = now;
    entry.inHazard = false;
    entry.eggPhase = false;
    entry.egg.Clear();
    if (bot->GetMapId() != OTHER_SIDE_MAP_ID || !bot->IsAlive() || !bot->IsInCombat())
        return entry;

    entry.inHazard = CollectHazards(bot).Contains(bot->GetPositionX(), bot->GetPositionY());
    Position center;
    entry.eggPhase = EggPhaseCenter(bot, center);
    if (entry.eggPhase)
        if (Creature* egg = EggToKick(bot))
            entry.egg = egg->GetGUID();
    return entry;
}
}

bool InHazard(Player* bot)
{
    return Refresh(bot).inHazard;
}

Creature* CachedEggToKick(Player* bot)
{
    Cached const& entry = Refresh(bot);
    return entry.egg ? ObjectAccessor::GetCreature(*bot, entry.egg) : nullptr;
}

bool InEggPhase(Player* bot)
{
    return Refresh(bot).eggPhase;
}
}
