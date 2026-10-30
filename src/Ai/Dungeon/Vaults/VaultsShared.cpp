/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "VaultsShared.h"

#include "Creature.h"
#include "DynamicObject.h"
#include "Group.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"
#include "Unit.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace VaultsShared
{
namespace
{
constexpr float SEARCH_RANGE = 60.0f;
constexpr float MARGIN = 2.5f;
constexpr float DEG = float(M_PI) / 180.0f;

// Radii as the core's script and Spell.dbc have them, plus MARGIN.
constexpr float CHAIN_RADIUS = 2.0f + MARGIN;         // Subjugating Chains
constexpr float CANDLE_RADIUS = 3.0f + MARGIN;        // CandleZoneRadius
constexpr float QUAKE_RADIUS = 10.0f + MARGIN;        // Earthquake, radius index 13 in the core
constexpr float INNER_RING_RADIUS = 9.5f + MARGIN;    // InnerRingRadius
constexpr float OUTER_RING_INNER = 9.5f - MARGIN;
constexpr float OUTER_RING_OUTER = 18.5f + MARGIN;    // OuterRingRadius
constexpr float WAX_RADIUS = 5.0f + MARGIN;           // Dark Wax Explosion
constexpr float SCREAM_RADIUS = 8.0f + MARGIN;        // Psychic Scream
constexpr float SHADOWFURY_RADIUS = 8.0f + MARGIN;
// Spectral Strike is TARGET_UNIT_CONE_ENEMY_24 without a spell_cone row: 24 degrees, 12 to each side.
// Voidflare has a 60 degree spell_cone row, as wide as it shows.
constexpr float STRIKE_HALF_ANGLE = 12.0f * DEG + 6.0f * DEG;
constexpr float VOIDFLARE_HALF_ANGLE = 30.0f * DEG + 6.0f * DEG;
constexpr float CONE_MIN_HALF_WIDTH = 2.0f;
constexpr float STRIKE_LENGTH = SEARCH_RANGE;
constexpr float VOIDFLARE_LENGTH = 40.0f + 2.0f;

// Who is in the soak or the shelter, with room to spare: SoakRadius 4.2, ShelterRadius 6.4.
constexpr float SOAK_HOLD = 3.5f;
constexpr float SHELTER_HOLD = 4.4f;
constexpr float SEAL_RANGE = 40.0f;
constexpr float SEAL_MASTER_RANGE = 30.0f;

constexpr uint32 CACHE_MS = 100;
constexpr uint32 IDLE_CACHE_MS = 1000;
constexpr uint32 RUN_KEEP_MS = 1500;
constexpr float AWAY_STEP = 8.0f;
constexpr float WAY_STEP = 1.0f;

float Signed(float angle)
{
    angle = Position::NormalizeOrientation(angle);
    return angle > float(M_PI) ? angle - 2.0f * float(M_PI) : angle;
}

uint32 Variant(Unit const* caster, uint32 base)
{
    return sSpellMgr->GetSpellIdForDifficulty(base, caster);
}

Spell* CurrentSpell(Unit* unit, uint32 base)
{
    uint32 const variant = Variant(unit, base);
    for (CurrentSpellTypes type : {CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL})
        if (Spell* spell = unit->GetCurrentSpell(type))
            if (spell->m_spellInfo->Id == base || spell->m_spellInfo->Id == variant)
                return spell;
    return nullptr;
}

bool HasAuraOf(Unit* unit, uint32 base)
{
    return unit->HasAura(base) || unit->HasAura(Variant(unit, base));
}

void AddCircle(Hazards& hazards, WorldObject const* at, float radius)
{
    hazards.circles.push_back({at->GetPositionX(), at->GetPositionY(), radius});
}

void AddCone(Hazards& hazards, WorldObject const* from, float angle, float length, float halfAngle)
{
    hazards.cones.push_back(
        {from->GetPositionX(), from->GetPositionY(), angle, length, halfAngle, CONE_MIN_HALF_WIDTH});
}

void AddEarthquake(Hazards& hazards, Creature* darkandle)
{
    if (CurrentSpell(darkandle, SPELL_EARTHQUAKE))
        AddCircle(hazards, darkandle, QUAKE_RADIUS);
    for (uint32 spell : {uint32(SPELL_EARTHQUAKE), Variant(darkandle, SPELL_EARTHQUAKE)})
        if (DynamicObject* quake = darkandle->GetDynObject(spell))
            hazards.circles.push_back({quake->GetPositionX(), quake->GetPositionY(), quake->GetRadius() + MARGIN});
}

void AddShadowfury(Hazards& hazards, Creature* shadow)
{
    Spell* spell = CurrentSpell(shadow, SPELL_SHADOWFURY);
    if (!spell)
        return;
    if (spell->m_targets.HasDst())
    {
        WorldLocation const* dst = spell->m_targets.GetDstPos();
        hazards.circles.push_back({dst->GetPositionX(), dst->GetPositionY(), SHADOWFURY_RADIUS});
    }
    else if (Unit* target = spell->m_targets.GetUnitTarget())
        AddCircle(hazards, target, SHADOWFURY_RADIUS);
}

std::list<Creature*> Nearby(Player* bot)
{
    std::list<Creature*> creatures;
    bot->GetCreatureListWithEntryInGrid(creatures,
        std::vector<uint32>{NPC_MERCILESS_ECHO, NPC_ECHO_IMAGE, NPC_CHAIN_ZONE, NPC_DARKANDLE, NPC_DECEIVER,
                            NPC_CANDLE_ZONE, NPC_FIGHT_CRYSTAL, NPC_SHELTER, NPC_PHANTASMAL_BOUND, NPC_BRUTE,
                            NPC_HELPER, NPC_CORPOREAL_SHADOW, NPC_BRUTE_ELITE, NPC_TRAP_CONTROLLER},
        SEARCH_RANGE);
    creatures.remove_if([](Creature* creature) { return !creature->IsAlive(); });
    return creatures;
}

Hazards HazardsFrom(Player* bot, std::list<Creature*> const& creatures)
{
    Hazards hazards;
    std::vector<Creature*> innerRings;
    bool outerRing = false;

    for (Creature* creature : creatures)
    {
        switch (creature->GetEntry())
        {
            case NPC_MERCILESS_ECHO:
                // He turns after his tank while he casts; the tank takes it, everyone else stays out of the line.
                if (CurrentSpell(creature, SPELL_SPECTRAL_STRIKE))
                    if (Unit* victim = creature->GetVictim())
                        if (victim != bot)
                            AddCone(hazards, creature, creature->GetAngle(victim), STRIKE_LENGTH, STRIKE_HALF_ANGLE);
                break;
            case NPC_ECHO_IMAGE:
                // An image faces its player when it starts and does not turn after, so that one can step aside too.
                if (CurrentSpell(creature, SPELL_SPECTRAL_STRIKE))
                    AddCone(hazards, creature, creature->GetOrientation(), STRIKE_LENGTH, STRIKE_HALF_ANGLE);
                break;
            case NPC_CHAIN_ZONE:
                AddCircle(hazards, creature, CHAIN_RADIUS);
                break;
            case NPC_CANDLE_ZONE:
                if (HasAuraOf(creature, SPELL_CANDLE_HIT_ZONE))
                    AddCircle(hazards, creature, CANDLE_RADIUS);
                break;
            case NPC_DARKANDLE:
                if (creature->IsInCombat())
                    AddEarthquake(hazards, creature);
                break;
            case NPC_DECEIVER:
                // Voidflare keeps the facing it started with.
                if (CurrentSpell(creature, SPELL_VOIDFLARE))
                    AddCone(hazards, creature, creature->GetOrientation(), VOIDFLARE_LENGTH, VOIDFLARE_HALF_ANGLE);
                break;
            case NPC_HELPER:
                if (creature->HasAura(SPELL_QUAKE_ZONE))
                    AddCircle(hazards, creature, QUAKE_RADIUS);
                else if (creature->HasAura(SPELL_SHADOWY_RING_INNER))
                    innerRings.push_back(creature);
                else if (creature->HasAura(SPELL_SHADOWY_RING_OUTER))
                {
                    outerRing = true;
                    hazards.rings.push_back(
                        {creature->GetPositionX(), creature->GetPositionY(), OUTER_RING_INNER, OUTER_RING_OUTER});
                }
                break;
            case NPC_BRUTE:
            case NPC_BRUTE_ELITE:
                if (CurrentSpell(creature, SPELL_DARK_WAX_EXPLOSION))
                    AddCircle(hazards, creature, WAX_RADIUS);
                break;
            case NPC_PHANTASMAL_BOUND:
                if (CurrentSpell(creature, SPELL_PSYCHIC_SCREAM))
                    AddCircle(hazards, creature, SCREAM_RADIUS);
                break;
            case NPC_CORPOREAL_SHADOW:
                AddShadowfury(hazards, creature);
                break;
            default:
                break;
        }
    }

    // The inner ring bursts as the outer one shows and lingers half a second; by then the inside is safe again.
    if (!outerRing)
        for (Creature* ring : innerRings)
            AddCircle(hazards, ring, INNER_RING_RADIUS);

    return hazards;
}

std::vector<Player*> Members(Player* bot)
{
    std::vector<Player*> members;
    if (Group* group = bot->GetGroup())
    {
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (member && member->IsAlive() && member->IsInMap(bot))
                members.push_back(member);
        }
    }
    else
        members.push_back(bot);
    return members;
}

// The bot nearest to `at`, a tank only when no other bot is there.
Player* NearestBot(std::vector<Player*> const& members, WorldObject const* at)
{
    Player* best = nullptr;
    Player* bestTank = nullptr;
    for (Player* member : members)
    {
        if (!GET_PLAYERBOT_AI(member))
            continue;
        Player*& slot = PlayerbotAI::IsTank(member) ? bestTank : best;
        if (!slot || member->GetExactDist2d(at) < slot->GetExactDist2d(at))
            slot = member;
    }
    return best ? best : bestTank;
}

bool Channeling(Player* player)
{
    return player->GetUInt32Value(UNIT_CHANNEL_SPELL) == SPELL_HOLY_CHANNEL_LOOK;
}

// Each seal crystal nobody breaks yet goes to the nearest bot that is free. Only once the group has come into
// the room: with a player leading, the player has to stand near the crystals.
Creature* SealFor(Player* bot)
{
    if (Channeling(bot))
        return nullptr;

    std::list<Creature*> found;
    bot->GetCreatureListWithEntryInGrid(found, NPC_SEAL_CRYSTAL, SEAL_RANGE);
    std::vector<Creature*> crystals;
    for (Creature* crystal : found)
        if (crystal->IsAlive())
            crystals.push_back(crystal);
    if (crystals.empty())
        return nullptr;

    if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
        if (Player* master = botAI->GetMaster())
            if (!GET_PLAYERBOT_AI(master) &&
                std::none_of(crystals.begin(), crystals.end(),
                             [master](Creature* crystal) { return master->GetExactDist2d(crystal) <= SEAL_MASTER_RANGE; }))
                return nullptr;

    std::vector<Player*> const members = Members(bot);
    std::vector<Player*> free;
    for (Player* member : members)
        if (GET_PLAYERBOT_AI(member) && !member->IsInCombat() && !Channeling(member))
            free.push_back(member);

    std::sort(crystals.begin(), crystals.end(),
              [](Creature* a, Creature* b) { return a->GetGUID().GetCounter() < b->GetGUID().GetCounter(); });
    for (Creature* crystal : crystals)
    {
        bool const taken = std::any_of(members.begin(), members.end(), [crystal](Player* member)
            { return member->GetGuidValue(UNIT_FIELD_CHANNEL_OBJECT) == crystal->GetGUID(); });
        if (taken)
            continue;
        Player* nearest = NearestBot(free, crystal);
        if (!nearest)
            return nullptr;
        if (nearest == bot)
            return crystal;
        free.erase(std::find(free.begin(), free.end(), nearest));
    }
    return nullptr;
}

// Out of a hazard, the way to the bot's target would take it back in: the target stands in one (an inner
// ring or Earthquake around the boss) or the line to it runs through one (a cone, the outer ring).
bool WayCrosses(Player* bot, Hazards const& hazards)
{
    if (hazards.Empty())
        return false;
    Unit* target = nullptr;
    if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
        target = botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
    if (!target)
        target = bot->GetVictim();
    if (!target || !target->IsInMap(bot))
        return false;

    float const x = bot->GetPositionX();
    float const y = bot->GetPositionY();
    float const dist = std::hypot(target->GetPositionX() - x, target->GetPositionY() - y);
    float const reach = bot->GetMeleeRange(target);
    if (dist <= reach)
        return false;
    float const dx = (target->GetPositionX() - x) / dist;
    float const dy = (target->GetPositionY() - y) / dist;
    for (float step = WAY_STEP; step <= dist - reach + WAY_STEP; step += WAY_STEP)
    {
        float const along = std::min(step, dist - reach);
        if (hazards.Contains(x + dx * along, y + dy * along))
            return true;
    }
    return false;
}

struct Cached
{
    uint32 readAt = 0;
    bool combat = false;
    bool inHazard = false;
    bool wayBlocked = false;
    bool holding = false;
    bool hasHold = false;
    Circle hold{};
    ObjectGuid soak;
    ObjectGuid shelter;
    ObjectGuid crystal;
    ObjectGuid seal;
    uint32 runAt = 0;
    Position run;
};

// Bots of different maps update on different threads.
Cached& Refresh(Player* bot)
{
    thread_local std::unordered_map<ObjectGuid, Cached> cache;
    uint32 const now = getMSTime();
    // Bots that left the map keep no entry for ever.
    if (cache.size() > 64)
        for (auto itr = cache.begin(); itr != cache.end();)
            itr = getMSTimeDiff(itr->second.readAt, now) > 10 * IN_MILLISECONDS ? cache.erase(itr) : std::next(itr);

    Cached& entry = cache[bot->GetGUID()];
    bool const combat = bot->IsInCombat();
    if (entry.readAt && entry.combat == combat &&
        getMSTimeDiff(entry.readAt, now) < (combat ? CACHE_MS : IDLE_CACHE_MS))
        return entry;

    entry.readAt = now;
    entry.combat = combat;
    entry.inHazard = false;
    entry.wayBlocked = false;
    entry.holding = false;
    entry.hasHold = false;
    entry.soak.Clear();
    entry.shelter.Clear();
    entry.crystal.Clear();
    entry.seal.Clear();
    if (bot->GetMapId() != VAULTS_MAP_ID || !bot->IsAlive())
        return entry;

    // Breaking a seal takes a 3 s channel.
    entry.holding = Channeling(bot);
    if (!combat)
    {
        if (Creature* seal = SealFor(bot))
            entry.seal = seal->GetGUID();
        return entry;
    }

    std::list<Creature*> const creatures = Nearby(bot);
    Hazards const hazards = HazardsFrom(bot, creatures);
    entry.inHazard = hazards.Contains(bot->GetPositionX(), bot->GetPositionY());
    entry.wayBlocked = !entry.inHazard && WayCrosses(bot, hazards);

    std::vector<Player*> const members = Members(bot);
    Creature* shelter = nullptr;
    bool voidWrath = false;
    for (Creature* creature : creatures)
    {
        switch (creature->GetEntry())
        {
            case NPC_DECEIVER:
                voidWrath = CurrentSpell(creature, SPELL_VOID_WRATH) || HasAuraOf(creature, SPELL_VOID_WRATH);
                break;
            case NPC_TRAP_CONTROLLER:
                // The soak circle wants one player in it, or Void Wrath hits everybody.
                if (bot->GetExactDist2d(creature) <= SOAK_HOLD)
                {
                    entry.holding = true;
                    entry.hasHold = true;
                    entry.hold = {creature->GetPositionX(), creature->GetPositionY(), SOAK_HOLD};
                }
                else if (std::none_of(members.begin(), members.end(),
                                      [creature](Player* member) { return member->GetExactDist2d(creature) <= SOAK_HOLD; }) &&
                         NearestBot(members, creature) == bot)
                    entry.soak = creature->GetGUID();
                break;
            case NPC_SHELTER:
                shelter = creature;
                break;
            default:
                break;
        }
    }

    // Void Wrath does not reach into the shelter: the whole group goes in once its 5 s cast starts, and fights
    // on until then.
    if (shelter && voidWrath)
    {
        if (bot->GetExactDist2d(shelter) <= SHELTER_HOLD)
        {
            entry.holding = true;
            entry.hasHold = true;
            entry.hold = {shelter->GetPositionX(), shelter->GetPositionY(), SHELTER_HOLD};
        }
        else
            entry.shelter = shelter->GetGUID();
    }
    else if (!shelter)
        for (Creature* creature : creatures)
            if (creature->GetEntry() == NPC_FIGHT_CRYSTAL && NearestBot(members, creature) == bot)
            {
                entry.crystal = creature->GetGUID();
                break;
            }

    return entry;
}

Creature* Get(Player* bot, ObjectGuid const& guid)
{
    return guid ? ObjectAccessor::GetCreature(*bot, guid) : nullptr;
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

    for (Ring const& ring : rings)
    {
        float const dist = std::hypot(x - ring.x, y - ring.y);
        if (dist >= ring.inner && dist <= ring.outer)
            return true;
    }

    for (Cone const& cone : cones)
    {
        float const dx = x - cone.x;
        float const dy = y - cone.y;
        float const dist = std::sqrt(dx * dx + dy * dy);
        if (dist > cone.length)
            continue;
        float const off = Signed(std::atan2(dy, dx) - cone.angle);
        if (std::fabs(off) <= cone.halfAngle)
            return true;
        if (dist * std::cos(off) > 0.0f && std::fabs(dist * std::sin(off)) <= cone.minHalfWidth)
            return true;
    }

    return false;
}

Hazards CollectHazards(Player* bot)
{
    return HazardsFrom(bot, Nearby(bot));
}

bool FindSafeSpot(Player* bot, Hazards const& hazards, Position& spot, Circle const* inside)
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
            // The cheap checks first: most points fail them, and only the rest cost a collision check.
            if (hazards.Contains(x, y))
                continue;
            if (inside && std::hypot(x - inside->x, y - inside->y) > inside->radius)
                continue;
            float z = bot->GetPositionZ();
            if (!bot->GetMap()->CheckCollisionAndGetValidCoords(bot, bot->GetPositionX(), bot->GetPositionY(),
                                                                bot->GetPositionZ(), x, y, z))
                continue;

            float const score = target ? std::fabs(target->GetExactDist2d(x, y) - keep) : 0.0f;
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

bool AwayFromHazard(Player* bot, Hazards const& hazards, Position& spot)
{
    float const x = bot->GetPositionX();
    float const y = bot->GetPositionY();
    float angle = 0.0f;
    float step = AWAY_STEP;
    bool found = false;

    for (Circle const& circle : hazards.circles)
        if (!found && std::hypot(x - circle.x, y - circle.y) <= circle.radius)
        {
            found = true;
            angle = std::atan2(y - circle.y, x - circle.x);
        }
    for (Ring const& ring : hazards.rings)
    {
        float const dist = std::hypot(x - ring.x, y - ring.y);
        if (found || dist < ring.inner || dist > ring.outer)
            continue;
        found = true;
        angle = std::atan2(y - ring.y, x - ring.x);
        // Whichever edge is closer: in toward the middle, or out past the ring.
        if (dist - ring.inner < ring.outer - dist)
        {
            angle += float(M_PI);
            step = std::min(AWAY_STEP, dist - ring.inner + 1.0f);
        }
    }
    for (Cone const& cone : hazards.cones)
    {
        if (found)
            break;
        float const off = Signed(std::atan2(y - cone.y, x - cone.x) - cone.angle);
        if (std::hypot(x - cone.x, y - cone.y) > cone.length || std::fabs(off) > cone.halfAngle)
            continue;
        found = true;
        angle = cone.angle + (off >= 0.0f ? 0.5f : -0.5f) * float(M_PI);
    }
    if (!found)
        return false;

    float toX = x + std::cos(angle) * step;
    float toY = y + std::sin(angle) * step;
    float toZ = bot->GetPositionZ();
    if (!bot->GetMap()->CheckCollisionAndGetValidCoords(bot, x, y, bot->GetPositionZ(), toX, toY, toZ))
        return false;

    spot.Relocate(toX, toY, toZ);
    return true;
}

bool HoldCircle(Player* bot, Circle& circle)
{
    Cached const& entry = Refresh(bot);
    if (!entry.hasHold)
        return false;
    circle = entry.hold;
    return true;
}

bool InHazard(Player* bot)
{
    return Refresh(bot).inHazard;
}

bool WayBlocked(Player* bot)
{
    return Refresh(bot).wayBlocked;
}

Creature* SoakToTake(Player* bot)
{
    return Get(bot, Refresh(bot).soak);
}

Creature* ShelterToReach(Player* bot)
{
    return Get(bot, Refresh(bot).shelter);
}

Creature* FightCrystalToClick(Player* bot)
{
    return Get(bot, Refresh(bot).crystal);
}

Creature* SealCrystalToBreak(Player* bot)
{
    return Get(bot, Refresh(bot).seal);
}

bool Holding(Player* bot)
{
    return Refresh(bot).holding;
}

bool KeepRunning(Player* bot, Hazards const& hazards, Position& spot)
{
    Cached const& entry = Refresh(bot);
    if (!entry.runAt || getMSTimeDiff(entry.runAt, getMSTime()) > RUN_KEEP_MS || !bot->isMoving())
        return false;
    if (hazards.Contains(entry.run.GetPositionX(), entry.run.GetPositionY()))
        return false;
    spot = entry.run;
    return true;
}

void RememberRun(Player* bot, Position const& spot)
{
    Cached& entry = Refresh(bot);
    entry.runAt = getMSTime();
    entry.run = spot;
}
}
