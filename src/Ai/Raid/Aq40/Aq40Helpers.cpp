/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "Aq40Helpers.h"
#include "Group.h"
#include "InstanceScript.h"
#include "ObjectAccessor.h"
#include "Playerbots.h"
#include "Spell.h"
#include "TemporarySummon.h"
#include "Timer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <list>
#include <unordered_map>

namespace Aq40Helpers
{

namespace
{

constexpr float PI_F = float(M_PI);

float AngleDelta(float to, float from) { return std::remainder(to - from, 2.0f * PI_F); }

// ---- Dark Glare -------------------------------------------------------------
// The glare hits anyone in front of the Eye less than 5 yd from its facing
// line (spell_cthun_dark_glare -> HasInLine), and boss_cthun_coa.cpp turns the
// line pi/35 every second, as stock does.
constexpr float GLARE_STEP = PI_F / 35.0f;
constexpr float GLARE_MIN_RADIUS = 15.0f;     // a dodge stays at least this far from the Eye
constexpr float GLARE_STEP_IN = 4.0f;         // where the floor runs out, step in this much at a time
constexpr float GLARE_FLOOR_MARGIN = 3.0f;    // floor needed past the dodge spot
// C'Thun's floor ends 48-64 yd from the Eye in places, over a 17 yd drop.
constexpr float FLOOR_PROBE_HEIGHT = 2.0f;
constexpr float FLOOR_TOLERANCE = 3.0f;

bool InGlareBand(float bearing, float distance, float line, float halfWidth)
{
    float const relative = AngleDelta(bearing, line);
    return std::abs(relative) < PI_F / 2.0f && std::abs(std::sin(relative)) * distance < halfWidth;
}

// In the band now or within the next `ticks` ticks (both ways while the direction is unknown).
bool InGlarePath(float bearing, float distance, float facing, float direction, float halfWidth, int ticks)
{
    for (int tick = 0; tick <= ticks; ++tick)
    {
        if (direction != 0.0f && InGlareBand(bearing, distance, facing + direction * GLARE_STEP * tick, halfWidth))
            return true;
        if (direction == 0.0f && (InGlareBand(bearing, distance, facing + GLARE_STEP * tick, halfWidth) ||
                                  InGlareBand(bearing, distance, facing - GLARE_STEP * tick, halfWidth)))
            return true;
    }
    return false;
}

// Bearing around the Eye to step to: ahead of the sweep for anyone it is coming
// toward, behind it for anyone it is leaving, and away from the line on the
// bot's own side while the direction is unknown.
float GlareEscapeBearing(float bearing, float distance, float facing, float direction, float halfWidth, int ticks)
{
    float const relative = AngleDelta(bearing, facing);
    float side = relative >= 0.0f ? 1.0f : -1.0f;
    bool const ahead = direction != 0.0f && relative * direction > 0.0f;
    if (ahead)
        side = direction;
    float const clear = std::asin(std::min(1.0f, halfWidth / std::max(distance, halfWidth)));
    float const lead = direction == 0.0f || ahead ? GLARE_STEP * float(ticks + 1) : GLARE_STEP;
    return facing + side * (clear + lead);
}

Position Around(Position const& center, float radius, float angle)
{
    return Position(center.GetPositionX() + radius * std::cos(angle), center.GetPositionY() + radius * std::sin(angle),
                    center.GetPositionZ());
}

bool OnCthunFloor(WorldObject* probe, float x, float y)
{
    return probe->GetMapHeight(x, y, CTHUN_CENTER.GetPositionZ() + FLOOR_PROBE_HEIGHT) >
           CTHUN_CENTER.GetPositionZ() - FLOOR_TOLERANCE;
}

// ---- Spots ------------------------------------------------------------------
// Offsets (x = north, y = west) from the Eye's center, on the walkable floor
// (z 100.7). Four melee stand on the Eye at N/W/S/E, inside its melee range;
// the middle ring (30-42 yd) keeps ranged in reach of the Eye; healers take the
// outer ring. At the CoA chain distance (15 yd) no group of linked spots is
// larger than four, and a smaller raid takes the spots farthest apart first.
enum SlotKind : uint8
{
    SLOT_EYE_MELEE,
    SLOT_MIDDLE,
    SLOT_OUTER,
    SLOT_KIND_COUNT
};

struct Slot
{
    float x;
    float y;
    SlotKind kind;
};

constexpr Slot CTHUN_SLOTS[] = {
    {16.5f, 0.0f, SLOT_EYE_MELEE},  {0.0f, 16.5f, SLOT_EYE_MELEE},  {-16.5f, 0.0f, SLOT_EYE_MELEE},
    {0.0f, -16.5f, SLOT_EYE_MELEE}, {-42.0f, -0.4f, SLOT_MIDDLE},   {-28.1f, -13.1f, SLOT_MIDDLE},
    {-28.9f, -30.5f, SLOT_MIDDLE},  {-14.3f, -26.4f, SLOT_MIDDLE},  {-12.5f, -40.1f, SLOT_MIDDLE},
    {2.9f, -34.4f, SLOT_MIDDLE},    {16.7f, -38.6f, SLOT_MIDDLE},   {20.6f, -21.8f, SLOT_MIDDLE},
    {34.5f, -24.0f, SLOT_MIDDLE},   {31.8f, -7.6f, SLOT_MIDDLE},    {42.0f, 0.1f, SLOT_MIDDLE},
    {26.7f, 13.7f, SLOT_MIDDLE},    {33.0f, 26.0f, SLOT_MIDDLE},    {15.1f, 25.9f, SLOT_MIDDLE},
    {12.5f, 40.1f, SLOT_MIDDLE},    {-1.5f, 42.0f, SLOT_MIDDLE},    {-14.0f, 39.6f, SLOT_MIDDLE},
    {-20.7f, 21.8f, SLOT_MIDDLE},   {-30.7f, 28.7f, SLOT_MIDDLE},   {-31.2f, 8.6f, SLOT_MIDDLE},
    {-62.9f, -11.7f, SLOT_OUTER},   {-48.3f, -16.7f, SLOT_OUTER},   {-45.0f, -30.7f, SLOT_OUTER},
    {10.0f, -53.0f, SLOT_OUTER},    {32.1f, -47.0f, SLOT_OUTER},    {50.1f, -35.0f, SLOT_OUTER},
    {61.5f, -17.7f, SLOT_OUTER},    {55.5f, -5.0f, SLOT_OUTER},     {43.0f, 15.5f, SLOT_OUTER},
    {34.5f, 50.0f, SLOT_OUTER},     {12.8f, 62.7f, SLOT_OUTER},     {-11.0f, 62.8f, SLOT_OUTER},
    {-32.5f, 51.0f, SLOT_OUTER},    {-45.5f, 33.0f, SLOT_OUTER},    {-47.0f, 20.6f, SLOT_OUTER},
    {-61.0f, 9.9f, SLOT_OUTER},
};
constexpr uint32 SLOT_COUNT = sizeof(CTHUN_SLOTS) / sizeof(CTHUN_SLOTS[0]);

// For each kind, its slots in the order a raid fills them: each next slot is
// the one farthest from every slot taken before it.
std::array<std::vector<uint32>, SLOT_KIND_COUNT> const& SlotOrder()
{
    static std::array<std::vector<uint32>, SLOT_KIND_COUNT> const order = []
    {
        std::array<std::vector<uint32>, SLOT_KIND_COUNT> result;
        std::vector<uint32> placed;
        for (uint8 kind = 0; kind < SLOT_KIND_COUNT; ++kind)
        {
            std::vector<uint32> left;
            for (uint32 i = 0; i < SLOT_COUNT; ++i)
                if (CTHUN_SLOTS[i].kind == kind)
                    left.push_back(i);
            while (!left.empty())
            {
                auto best = left.begin();
                float bestScore = -1.0f;
                for (auto it = left.begin(); it != left.end(); ++it)
                {
                    float score = 1000.0f;
                    for (uint32 other : placed)
                        score = std::min(score, std::hypot(CTHUN_SLOTS[*it].x - CTHUN_SLOTS[other].x,
                                                           CTHUN_SLOTS[*it].y - CTHUN_SLOTS[other].y));
                    if (score > bestScore)
                    {
                        bestScore = score;
                        best = it;
                    }
                }
                result[kind].push_back(*best);
                placed.push_back(*best);
                left.erase(best);
            }
        }
        return result;
    }();
    return order;
}

bool IsTankRole(Player* member) { return PlayerbotAI::IsTank(member); }

SlotKind RoleKind(Player* member)
{
    if (PlayerbotAI::IsHeal(member))
        return SLOT_OUTER;
    if (PlayerbotAI::IsMelee(member) || IsTankRole(member))
        return SLOT_EYE_MELEE;
    return SLOT_MIDDLE;
}

// The slot index dealt to `bot`, or -1.
int32 DealSlot(Player* bot)
{
    Group* group = bot->GetGroup();
    if (!group)
        return -1;

    std::vector<Player*> members;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        if (Player* member = ref->GetSource())
            if (member->GetMapId() == bot->GetMapId())
                members.push_back(member);
    std::sort(members.begin(), members.end(),
              [](Player* a, Player* b) { return a->GetGUID().GetCounter() < b->GetGUID().GetCounter(); });

    auto const& order = SlotOrder();
    std::array<bool, SLOT_COUNT> taken{};
    std::unordered_map<ObjectGuid, uint32> dealt;
    auto claim = [&](Player* member, std::initializer_list<SlotKind> kinds)
    {
        for (SlotKind kind : kinds)
            for (uint32 index : order[kind])
                if (!taken[index])
                {
                    taken[index] = true;
                    dealt[member->GetGUID()] = index;
                    return;
                }
    };

    // Damage-dealing melee on the Eye first, tanks only if short of melee;
    // healers outside; ranged and the remaining melee in the middle ring.
    for (bool tanks : {false, true})
        for (Player* member : members)
            if (RoleKind(member) == SLOT_EYE_MELEE && IsTankRole(member) == tanks)
                claim(member, {SLOT_EYE_MELEE});
    for (Player* member : members)
        if (RoleKind(member) == SLOT_OUTER)
            claim(member, {SLOT_OUTER, SLOT_MIDDLE});
    for (Player* member : members)
        if (!dealt.count(member->GetGUID()))
            claim(member, {SLOT_MIDDLE, SLOT_OUTER, SLOT_EYE_MELEE});

    auto const it = dealt.find(bot->GetGUID());
    return it == dealt.end() ? -1 : int32(it->second);
}

// ---- Shared caches ------------------------------------------------------------
// A map and the bots in it are updated by one thread, so per-thread caches need
// no lock (as GetMcBossAdds).

struct GlareState
{
    float lastOrientation = 0.0f;
    uint32 seenMs = 0;
    float direction = 0.0f;
};

struct EldritchState
{
    ObjectGuid caster;
    uint32 untilMs = 0;
};

constexpr uint32 UNIT_CACHE_MS = 500;
constexpr uint32 ELDRITCH_AFTER_CAST_MS = 2600;  // pulses at 0, 1 and 2s after the cast
constexpr float LESSER_ELDRITCH_RANGE = 42.0f;   // Revelation(me, 40.0f) plus a margin

}  // namespace

bool IsInAq40(Player* bot) { return bot && bot->GetMapId() == AQ40_MAP_ID; }

Creature* GetEye(Player* bot)
{
    InstanceScript* instance = IsInAq40(bot) ? bot->GetInstanceScript() : nullptr;
    return instance ? instance->GetCreature(DATA_EYE_OF_CTHUN) : nullptr;
}

Creature* GetCthun(Player* bot)
{
    InstanceScript* instance = IsInAq40(bot) ? bot->GetInstanceScript() : nullptr;
    return instance ? instance->GetCreature(DATA_CTHUN) : nullptr;
}

bool InStomach(Unit const* unit)
{
    // boss_cthun_coa.cpp: the acid aura, or below the room (the stomach is under it).
    if (!unit)
        return false;
    if (unit->HasAura(SPELL_DIGESTIVE_ACID))
        return true;
    return unit->GetMapId() == AQ40_MAP_ID && unit->GetPositionZ() < 0.0f &&
           unit->GetExactDist2d(CTHUN_CENTER.GetPositionX(), CTHUN_CENTER.GetPositionY()) < 120.0f;
}

bool IsCthunBody(Unit const* unit) { return unit && unit->GetEntry() == NPC_CTHUN; }

bool IsCthunEncounter(Player* bot)
{
    if (!IsInAq40(bot))
        return false;
    if (!InStomach(bot) && bot->GetExactDist2d(CTHUN_CENTER.GetPositionX(), CTHUN_CENTER.GetPositionY()) > CTHUN_ROOM_RADIUS)
        return false;
    Creature* eye = GetEye(bot);
    if (eye && eye->IsAlive() && eye->IsInCombat())
        return true;
    Creature* cthun = GetCthun(bot);
    return cthun && cthun->IsAlive() && cthun->IsInCombat();
}

bool IsCthunPhaseTwo(Player* bot)
{
    // The Eye fakes its death (0 health, still alive) when the body rises.
    Creature* eye = GetEye(bot);
    return !eye || eye->GetHealth() == 0;
}

bool IsCthunWeakened(Player* bot)
{
    Creature* cthun = GetCthun(bot);
    return cthun && cthun->HasAura(SPELL_WEAKENING);
}

std::vector<Creature*> GetCthunUnits(Player* bot, uint32 entry)
{
    struct UnitCache
    {
        uint32 refreshedAt = 0;
        std::vector<ObjectGuid> guids;
    };
    thread_local std::unordered_map<uint64, UnitCache> caches;

    bool const stomach = InStomach(bot);
    uint64 const key = (uint64(bot->GetInstanceId()) << 32) | (uint64(entry) << 1) | (stomach ? 1 : 0);
    uint32 const now = getMSTime();
    auto it = caches.find(key);
    if (it == caches.end() || getMSTimeDiff(it->second.refreshedAt, now) >= UNIT_CACHE_MS)
    {
        for (auto stale = caches.begin(); stale != caches.end();)
        {
            if (getMSTimeDiff(stale->second.refreshedAt, now) >= 60000u)
                stale = caches.erase(stale);
            else
                ++stale;
        }

        std::list<Creature*> found;
        bot->GetCreatureListWithEntryInGrid(found, entry, stomach ? 80.0f : CTHUN_ROOM_RADIUS);
        UnitCache& cache = caches[key];
        cache.refreshedAt = now;
        cache.guids.clear();
        for (Creature* creature : found)
            if (creature->IsAlive() && InStomach(creature) == stomach)
                cache.guids.push_back(creature->GetGUID());
        it = caches.find(key);
    }

    std::vector<Creature*> units;
    for (ObjectGuid const& guid : it->second.guids)
        if (Creature* creature = ObjectAccessor::GetCreature(*bot, guid))
            if (creature->IsAlive())
                units.push_back(creature);
    return units;
}

bool GetCthunSpot(Player* bot, Position& spot)
{
    if (InStomach(bot))
        return false;

    int32 const index = DealSlot(bot);
    if (index < 0)
        return false;

    Slot const& slot = CTHUN_SLOTS[index];
    float radius = std::hypot(slot.x, slot.y);
    float const angle = std::atan2(slot.y, slot.x);

    // CoA keeps the phase-1 spread in phase 2: the Giant Eye's Lesser Eye Beam
    // chains like the Eye Beam, the mouth swallows everyone within 8 yd of its
    // prey and the Offering punishes allies within 10 yd. Only the Weakening
    // window pulls the damage dealers in, where they can reach C'Thun.
    if (IsCthunPhaseTwo(bot) && IsCthunWeakened(bot) && !PlayerbotAI::IsHeal(bot))
    {
        if (PlayerbotAI::IsMelee(bot) || PlayerbotAI::IsTank(bot))
            radius = CTHUN_BURN_MELEE_RADIUS;
        else
            radius = std::min(radius, CTHUN_BURN_RANGED_RADIUS);
    }

    spot = Around(CTHUN_CENTER, radius, angle);
    return true;
}

bool IsEyeMeleeSpot(Player* bot)
{
    int32 const index = DealSlot(bot);
    return index >= 0 && CTHUN_SLOTS[index].kind == SLOT_EYE_MELEE;
}

float GetGlareDirection([[maybe_unused]] Player* bot, Unit* eye)
{
    thread_local std::unordered_map<ObjectGuid, GlareState> states;

    uint32 const now = getMSTime();
    GlareState& state = states[eye->GetGUID()];
    float const orientation = eye->GetOrientation();
    // A new glare (the last one ended long ago): the direction is unknown again.
    if (getMSTimeDiff(state.seenMs, now) > 3000)
    {
        state.direction = 0.0f;
        state.lastOrientation = orientation;
    }
    else
    {
        float const turned = AngleDelta(orientation, state.lastOrientation);
        if (std::abs(turned) > 0.01f && std::abs(turned) < 3.0f * GLARE_STEP)
            state.direction = turned > 0.0f ? 1.0f : -1.0f;
        state.lastOrientation = orientation;
    }
    state.seenMs = now;
    return state.direction;
}

bool IsInGlarePath(Player* bot, Unit* eye)
{
    float const bearing = eye->GetAngle(bot);
    float const distance = eye->GetExactDist2d(bot);
    return InGlarePath(bearing, distance, eye->GetOrientation(), GetGlareDirection(bot, eye), GLARE_HALF_WIDTH,
                       GLARE_LOOKAHEAD_TICKS);
}

Position GetGlareDodge(Player* bot, Unit* eye)
{
    // Dodging at the bot's own distance carries outer-ring bots past the floor's
    // edge. Step in until the dodge spot, a margin beyond it and the way there
    // all have floor; the escape bearing is recomputed for each radius.
    float const bearing = eye->GetAngle(bot);
    float const direction = GetGlareDirection(bot, eye);
    Position const center = eye->GetPosition();
    Position goal;
    float radius = std::max(GLARE_MIN_RADIUS, eye->GetExactDist2d(bot));
    while (true)
    {
        float const escape = GlareEscapeBearing(bearing, radius, eye->GetOrientation(), direction, GLARE_HALF_WIDTH,
                                                GLARE_LOOKAHEAD_TICKS);
        goal = Around(center, radius, escape);
        Position const beyond = Around(center, radius + GLARE_FLOOR_MARGIN, escape);
        if (radius <= GLARE_MIN_RADIUS ||
            (OnCthunFloor(bot, beyond.GetPositionX(), beyond.GetPositionY()) &&
             OnCthunFloor(bot, (bot->GetPositionX() + goal.GetPositionX()) / 2.0f,
                          (bot->GetPositionY() + goal.GetPositionY()) / 2.0f)))
            break;
        radius = std::max(GLARE_MIN_RADIUS, radius - GLARE_STEP_IN);
    }
    goal.m_positionZ = bot->GetPositionZ();
    return goal;
}

Unit* GetEldritchCaster(Player* bot)
{
    thread_local std::unordered_map<ObjectGuid, EldritchState> states;

    if (InStomach(bot))
        return nullptr;

    uint32 const now = getMSTime();
    Unit* caster = nullptr;
    if (Creature* eye = GetEye(bot))
        if (eye->IsAlive() && eye->GetHealth() > 0 && eye->FindCurrentSpellBySpellId(SPELL_ELDRITCH_REVELATIONS))
            caster = eye;
    if (!caster)
        for (Creature* giantEye : GetCthunUnits(bot, NPC_GIANT_EYE_TENTACLE))
            if (giantEye->FindCurrentSpellBySpellId(SPELL_LESSER_ELDRITCH_REVELATIONS) &&
                bot->GetExactDist2d(giantEye) <= LESSER_ELDRITCH_RANGE)
            {
                caster = giantEye;
                break;
            }

    if (caster)
    {
        states[bot->GetGUID()] = {caster->GetGUID(), now + ELDRITCH_AFTER_CAST_MS};
        return caster;
    }

    auto it = states.find(bot->GetGUID());
    if (it == states.end())
        return nullptr;
    if (int32(it->second.untilMs - now) <= 0)
    {
        states.erase(it);
        return nullptr;
    }
    Unit* last = ObjectAccessor::GetUnit(*bot, it->second.caster);
    return last && last->IsAlive() ? last : nullptr;
}

bool ShouldLeaveStomach(Player* bot)
{
    if (!InStomach(bot))
        return false;
    // Every second a raid member spends inside heals C'Thun for 10% of that
    // member's maximum health (boss_cthun_coa.cpp, Digest). Tanks and healers
    // leave at once; damage dealers once the Flesh Tentacles are dead, or
    // before the acid (1% of current health per second) takes them too low.
    if (PlayerbotAI::IsTank(bot) || PlayerbotAI::IsHeal(bot))
        return true;
    return GetCthunUnits(bot, NPC_FLESH_TENTACLE).empty() || bot->GetHealthPct() < 40.0f;
}

Unit* ChooseCthunTarget(Player* bot)
{
    auto attackable = [bot](Unit* unit)
    { return unit && unit->IsAlive() && unit->isTargetableForAttack() && bot->IsValidAttackTarget(unit); };

    if (InStomach(bot))
    {
        Creature* best = nullptr;
        for (Creature* flesh : GetCthunUnits(bot, NPC_FLESH_TENTACLE))
            if (attackable(flesh) && (!best || bot->GetDistance(flesh) < bot->GetDistance(best)))
                best = flesh;
        return best;
    }

    if (PlayerbotAI::IsHeal(bot))
        return nullptr;

    Position spot;
    bool const haveSpot = GetCthunSpot(bot, spot);
    bool const melee = PlayerbotAI::IsMelee(bot) || PlayerbotAI::IsTank(bot);
    bool const phase2 = IsCthunPhaseTwo(bot);
    bool const weakened = phase2 && IsCthunWeakened(bot);
    bool const eyeMelee = !phase2 && IsEyeMeleeSpot(bot);
    auto nearSpot = [&](Unit* unit, float leash)
    { return haveSpot && unit->GetExactDist2d(&spot) <= leash + unit->GetCombatReach(); };
    auto heldByTank = [](Unit* unit)
    {
        Unit* victim = unit->GetVictim();
        Player* player = victim ? victim->ToPlayer() : nullptr;
        return player && PlayerbotAI::IsTank(player);
    };

    // A tank takes a Giant Claw near the raid that no tank holds yet.
    if (PlayerbotAI::IsTank(bot))
        for (Creature* claw : GetCthunUnits(bot, NPC_GIANT_CLAW_TENTACLE))
            if (attackable(claw) && nearSpot(claw, CTHUN_FAR_CLAW) && (claw->GetVictim() == bot || !heldByTank(claw)))
                return claw;

    // God of Madness: every tentacle that dies deals its health to the Eye (or
    // to C'Thun, shield or no shield), so the tentacles come first. C'Thun
    // himself only takes real damage while Weakened (-99% under the shield).
    std::vector<uint32> const priorities =
        weakened ? std::vector<uint32>{NPC_GIANT_EYE_TENTACLE, NPC_GIANT_CLAW_TENTACLE, NPC_CTHUN, NPC_EYE_TENTACLE,
                                       NPC_CLAW_TENTACLE}
                 : std::vector<uint32>{NPC_GIANT_EYE_TENTACLE, NPC_EYE_TENTACLE, NPC_GIANT_CLAW_TENTACLE,
                                       NPC_CLAW_TENTACLE, NPC_EYE_OF_CTHUN};

    for (uint32 entry : priorities)
    {
        std::vector<Creature*> units;
        if (entry == NPC_CTHUN || entry == NPC_EYE_OF_CTHUN)
        {
            Creature* boss = entry == NPC_CTHUN ? GetCthun(bot) : GetEye(bot);
            if (boss && (entry == NPC_CTHUN || boss->GetHealth() > 0))
                units.push_back(boss);
        }
        else
            units = GetCthunUnits(bot, entry);

        Creature* best = nullptr;
        for (Creature* unit : units)
        {
            if (!attackable(unit))
                continue;
            if (entry == NPC_GIANT_CLAW_TENTACLE && (!nearSpot(unit, CTHUN_FAR_CLAW) || (weakened && heldByTank(unit))))
                continue;
            if (entry == NPC_CTHUN)
            {
                // Melee walk to the burn ring (GetCthunSpot); ranged hit him from theirs.
                if (!melee && bot->GetDistance(unit) > CTHUN_RANGED_REACH)
                    continue;
            }
            else if (entry == NPC_EYE_OF_CTHUN)
            {
                // Only the four Eye spots stand in its melee range.
                if (melee ? !eyeMelee : bot->GetDistance(unit) > CTHUN_RANGED_REACH)
                    continue;
            }
            else if (melee ? !nearSpot(unit, CTHUN_MELEE_LEASH) : bot->GetDistance(unit) > CTHUN_RANGED_REACH)
                continue;

            if (!best || bot->GetDistance(unit) < bot->GetDistance(best))
                best = unit;
        }
        if (best)
            return best;
    }
    return nullptr;
}

Player* GetDevourPreyNear(Player* bot)
{
    Group* group = bot->GetGroup();
    if (!group || InStomach(bot))
        return nullptr;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == bot || !member->IsAlive() || member->GetMapId() != bot->GetMapId())
            continue;
        if (member->HasAura(SPELL_DEVOURS) && bot->GetExactDist2d(member) < DEVOUR_CLEAR_DISTANCE)
            return member;
    }
    return nullptr;
}

// ---- The Prophet Skeram -----------------------------------------------------

Creature* GetSkeram(Player* bot)
{
    struct SkeramCache
    {
        ObjectGuid guid;
        uint32 searchedAt = 0;
    };
    thread_local std::unordered_map<uint32, SkeramCache> caches;

    if (!IsInAq40(bot))
        return nullptr;
    SkeramCache& cache = caches[bot->GetInstanceId()];
    if (!cache.guid.IsEmpty())
        if (Creature* skeram = ObjectAccessor::GetCreature(*bot, cache.guid))
            return skeram;

    // Searched only by bots in combat, and at most twice a second per instance.
    if (!bot->IsInCombat())
        return nullptr;
    uint32 const now = getMSTime();
    if (cache.searchedAt && getMSTimeDiff(cache.searchedAt, now) < UNIT_CACHE_MS)
        return nullptr;
    cache.searchedAt = now;

    std::list<Creature*> found;
    bot->GetCreatureListWithEntryInGrid(found, NPC_SKERAM, SKERAM_SEARCH_RANGE);
    for (Creature* creature : found)
        if (!creature->IsSummon())
        {
            cache.guid = creature->GetGUID();
            return creature;
        }
    return nullptr;
}

bool IsSkeram(Unit const* unit) { return unit && unit->GetEntry() == NPC_SKERAM; }

bool IsSkeramEncounter(Player* bot)
{
    Creature* skeram = GetSkeram(bot);
    return skeram && skeram->IsAlive() && skeram->IsInCombat() && bot->GetExactDist2d(skeram) <= SKERAM_SEARCH_RANGE;
}

bool IsSkeramVeiled(Player* bot)
{
    Creature* skeram = GetSkeram(bot);
    return skeram && skeram->HasAura(SPELL_NIGHTMARE_VEIL);
}

std::vector<Creature*> GetSkeramUnits(Player* bot, uint32 entry)
{
    struct UnitCache
    {
        uint32 refreshedAt = 0;
        std::vector<ObjectGuid> guids;
    };
    thread_local std::unordered_map<uint64, UnitCache> caches;

    std::vector<Creature*> units;
    Creature* skeram = GetSkeram(bot);
    if (!skeram)
        return units;

    // Searched around Skeram, so every bot of the instance reads the same list.
    uint64 const key = (uint64(bot->GetInstanceId()) << 32) | entry;
    uint32 const now = getMSTime();
    auto it = caches.find(key);
    if (it == caches.end() || getMSTimeDiff(it->second.refreshedAt, now) >= UNIT_CACHE_MS)
    {
        for (auto stale = caches.begin(); stale != caches.end();)
        {
            if (getMSTimeDiff(stale->second.refreshedAt, now) >= 60000u)
                stale = caches.erase(stale);
            else
                ++stale;
        }

        std::list<Creature*> found;
        skeram->GetCreatureListWithEntryInGrid(found, entry, SKERAM_SEARCH_RANGE);
        UnitCache& cache = caches[key];
        cache.refreshedAt = now;
        cache.guids.clear();
        for (Creature* creature : found)
            if (creature->IsAlive())
                cache.guids.push_back(creature->GetGUID());
        it = caches.find(key);
    }

    for (ObjectGuid const& guid : it->second.guids)
        if (Creature* creature = ObjectAccessor::GetCreature(*bot, guid))
            if (creature->IsAlive())
                units.push_back(creature);
    return units;
}

int32 GetPhobia(Unit const* unit)
{
    if (!unit)
        return -1;
    for (int32 phobia = 0; phobia < SKERAM_PHOBIAS; ++phobia)
        if (unit->HasAura(SPELL_PHOBIA_FIRST + uint32(phobia)))
            return phobia;
    return -1;
}

bool ShouldHoldOnSkeram(Player* bot)
{
    // boss_skeram_coa.cpp DamageTaken: a quarter of the damage he (or an image)
    // takes is dealt to his worshipper, who is pacified and rooted meanwhile.
    Group* group = bot->GetGroup();
    if (!group)
        return false;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (member && member->IsAlive() && member->GetMapId() == bot->GetMapId() && member->HasAura(SPELL_WORSHIP) &&
            member->GetHealthPct() < WORSHIP_HOLD_PCT)
            return true;
    }
    return false;
}

namespace
{
// Whether a damage dealer shares this phobia: healers of such a group heal it.
bool PhobiaHasDamageDealer(Player* bot, int32 phobia)
{
    Group* group = bot->GetGroup();
    if (!group)
        return false;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (member && member != bot && member->IsAlive() && member->GetMapId() == bot->GetMapId() &&
            !PlayerbotAI::IsHeal(member) && GetPhobia(member) == phobia)
            return true;
    }
    return false;
}
}  // namespace

Unit* ChooseSkeramTarget(Player* bot)
{
    auto attackable = [bot](Unit* unit)
    {
        return unit && unit->IsAlive() && bot->InSamePhase(unit) && unit->isTargetableForAttack() &&
               bot->IsValidAttackTarget(unit);
    };

    // In a nightmare only the creatures of the bot's own phobia can be seen and
    // killed, and killing them is the only way out; everyone fights them,
    // healers too when their group has no damage dealer.
    int32 const phobia = GetPhobia(bot);
    if (phobia >= 0)
    {
        if (PlayerbotAI::IsHeal(bot) && PhobiaHasDamageDealer(bot, phobia))
            return nullptr;
        Creature* best = nullptr;
        for (Creature* dread : GetSkeramUnits(bot, NPC_NIGHTMARE_FIRST + uint32(phobia)))
            if (attackable(dread) && (!best || dread->GetHealth() < best->GetHealth()))
                best = dread;
        return best;
    }

    if (PlayerbotAI::IsHeal(bot) || PlayerbotAI::IsTank(bot) || IsSkeramVeiled(bot) || ShouldHoldOnSkeram(bot))
        return nullptr;

    // His images take 250-750% more damage and cast his whole kit: they go
    // first, the weakest one at a time. Skeram himself is left to the generic
    // targeting.
    Creature* best = nullptr;
    for (Creature* image : GetSkeramUnits(bot, NPC_SKERAM))
        if (image->IsSummon() && attackable(image) && (!best || image->GetHealth() < best->GetHealth()))
            best = image;
    return best;
}

Creature* GetPsychicShockSpotNear(Player* bot)
{
    for (Creature* spot : GetSkeramUnits(bot, NPC_SKERAM_SHOCK_SPOT))
    {
        if (!spot->IsSummon() || !bot->InSamePhase(spot) || bot->GetExactDist2d(spot) >= SHOCK_CLEAR_DISTANCE)
            continue;
        Unit* summoner = spot->ToTempSummon()->GetSummonerUnit();
        if (summoner && summoner->GetEntry() == NPC_SKERAM)
            return spot;
    }
    return nullptr;
}

bool ShouldSpreadFromGaze(Player* bot)
{
    // Only ranged and healers spread: melee share his hitbox anyway.
    if (PlayerbotAI::IsMelee(bot) || PlayerbotAI::IsTank(bot) || GetPhobia(bot) >= 0 || IsSkeramVeiled(bot))
        return false;
    Group* group = bot->GetGroup();
    if (!group)
        return false;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (member && member != bot && member->IsAlive() && member->GetMapId() == bot->GetMapId() &&
            bot->InSamePhase(member) && bot->GetExactDist2d(member) < GAZE_SPREAD_DISTANCE)
            return true;
    }
    return false;
}

}  // namespace Aq40Helpers
