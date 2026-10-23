/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

/*
 * Road to De Other Side (map 937), Ascension's Sinister Lair dungeon as the CoA core rebuilds it
 * (src/server/coa/CoAOtherSide*.cpp). Nearly every mechanic there is a helper creature on the
 * ground: a swirl, a glaive, a pillar, an orb, a spike mark, a web line. CollectHazards() turns
 * them into circles, strips and the sweep of Drak'math's Death Beam; FindSafeSpot() picks the
 * nearest point outside all of them at once, so stepping out of one never walks into the next.
 */

#ifndef PLAYERBOTS_OTHERSIDESHARED_H
#define PLAYERBOTS_OTHERSIDESHARED_H

#include "Common.h"
#include "Position.h"

#include <vector>

class Creature;
class Player;
class Unit;

namespace OtherSideShared
{
inline constexpr uint32 OTHER_SIDE_MAP_ID = 937;

enum OtherSideCreatures : uint32
{
    NPC_MUZAH = 162711,
    NPC_DRAKMATH = 162715,
    NPC_EMBRYO = 162716,
    NPC_MUZAH_GLAIVE = 162744,
    NPC_MUZAH_SWIRL = 162779,     // the bat crash swirl, and on Mythic the Glaive Strike thrower
    NPC_DRAKMATH_PILLAR = 162727, // pillar, Death Missile mark and the walker the beam follows
    NPC_DRAKMATH_ORB = 162728,
    NPC_EMBRYO_POISON = 162739,   // the barrier in the middle and the two swirls around it
    NPC_EMBRYO_WEB_MARK = 162740,
    NPC_EMBRYO_WEB = 162741,
    NPC_EMBRYO_SPIKE = 162973,
    NPC_EMBRYO_EGG = 9370010,
    NPC_EMBRYO_SPIDER = 162743,
};

enum OtherSideSpells : uint32
{
    SPELL_GROUND_ZONE = 256852,  // Muzah's crash swirl and the Embryo's spike marks
    SPELL_GLAIVE_STRIKE = 257029,
    SPELL_MUZAHS_TARGET = 256964, // "Close the distance to force him to stop firing his bow."
    SPELL_ENTANGLING_WEB = 256834,
    SPELL_ENTANGLING_WEB_HC = 257039,
    SPELL_ENTANGLING_WEB_MYTHIC = 257040,
    SPELL_PILLAR = 256854,
    SPELL_IMPACT_ZONE = 256865,
    SPELL_DEATH_BEAM = 256858,
    SPELL_POISON_CIRCLE = 256875,
    SPELL_POISON_SWIRL = 256883,
    SPELL_WEB_ACTIVE = 256880,
};

struct Circle
{
    float x, y, radius;
};

// A band from (x, y) along angle: Glaive Strike, a web line, the path of an orb.
struct Strip
{
    float x, y, angle, length, halfWidth;
};

// Drak'math's Death Beam: a cone that turns; behind it is safe, ahead of it is not.
struct Sweep
{
    float x, y, angle, length, minHalfWidth, back, ahead;
};

struct Hazards
{
    std::vector<Circle> circles;
    std::vector<Strip> strips;
    std::vector<Sweep> sweeps;

    bool Empty() const { return circles.empty() && strips.empty() && sweeps.empty(); }
    bool Contains(float x, float y) const;
};

Hazards CollectHazards(Player* bot);

// The nearest reachable point outside every hazard, preferring one that keeps the bot's distance
// to its target, or with `toward` the one closest to that point. False when there is none in reach.
bool FindSafeSpot(Player* bot, Hazards const& hazards, Position& spot, Position const* toward = nullptr);

// The middle of the Embryo's barrier while the egg phase lasts.
bool EggPhaseCenter(Player* bot, Position& center);

// In the egg phase the group runs counterclockwise along the wall, outside the reach of the poison swirls:
// the next point on that way.
Position NextRunSpot(Player* bot, Position const& center);

// Eggs are kicked on the way only: each egg a little ahead of the group (counterclockwise) goes to the
// member who comes up to it first. The egg this bot should step on, if any.
Creature* EggToKick(Player* bot);

// The nearest hatched spider that has come for the group, or nullptr.
Unit* NearestSpider(Player* bot);

// Both of the above, read once per bot every 200 ms: triggers and the multiplier ask for them on
// every evaluation.
bool InHazard(Player* bot);
Creature* CachedEggToKick(Player* bot);
bool InEggPhase(Player* bot);
}

#endif
