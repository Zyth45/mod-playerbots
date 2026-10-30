/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

/*
 * Vaults of the Inquisition (map 936), Ascension's dungeon under Northshire Abbey as the CoA core rebuilds
 * it (src/server/coa/CoAVaults.cpp). Like Road to De Other Side, most mechanics there are helper creatures
 * on the ground or casts of a known shape. CollectHazards() turns them into circles, rings and cones;
 * FindSafeSpot() picks the nearest point outside all of them at once. Three mechanics want the group in a
 * place instead: the soak circle under a player, the shelter of the Holy Crystal against Void Wrath, and
 * the three Holy Crystals that seal the Deceiver before the fight.
 */

#ifndef PLAYERBOTS_VAULTSSHARED_H
#define PLAYERBOTS_VAULTSSHARED_H

#include "Common.h"
#include "Position.h"

#include <vector>

class Creature;
class Player;
class Unit;

namespace VaultsShared
{
inline constexpr uint32 VAULTS_MAP_ID = 936;

enum VaultsCreatures : uint32
{
    NPC_MERCILESS_ECHO = 162700,
    NPC_ECHO_IMAGE = 162701,
    NPC_CHAIN_ZONE = 162702,
    NPC_DARKANDLE = 162703,
    NPC_DECEIVER = 162704,
    NPC_SEAL_CRYSTAL = 162705,    // the three that hold the Deceiver before the fight
    NPC_CANDLE_ZONE = 162707,     // a More Fire! circle the candles shoot at
    NPC_FIGHT_CRYSTAL = 162723,   // "A holy crystal appears to help you!"
    NPC_SHELTER = 162724,         // the Holy Shelter the clicked crystal leaves
    NPC_PHANTASMAL_BOUND = 162734,
    NPC_BRUTE = 162738,
    NPC_HELPER = 162745,          // Earthquake marker and the Deceiver's Shadowy Rings, among others
    NPC_CORPOREAL_SHADOW = 162761,
    NPC_BRUTE_ELITE = 162763,
    NPC_TRAP_CONTROLLER = 162978, // the soak circle
};

// Base ids; the core casts their Heroic and Mythic variants from SpellDifficulty.
enum VaultsSpells : uint32
{
    SPELL_SPECTRAL_STRIKE = 256975,
    SPELL_CANDLE_HIT_ZONE = 256815,
    SPELL_EARTHQUAKE = 256819,
    SPELL_QUAKE_ZONE = 256917,
    SPELL_SHADOWY_RING_INNER = 256821,
    SPELL_SHADOWY_RING_OUTER = 256822,
    SPELL_VOIDFLARE = 256927,
    SPELL_VOID_WRATH = 256844,
    SPELL_DARK_WAX_EXPLOSION = 256893,
    SPELL_PSYCHIC_SCREAM = 257166,
    SPELL_SHADOWFURY = 256491,
    SPELL_HOLY_CHANNEL_LOOK = 47540, // the channel a player shows while breaking a seal crystal
};

struct Circle
{
    float x, y, radius;
};

// Between two radii around (x, y): the Deceiver's outer Shadowy Ring.
struct Ring
{
    float x, y, inner, outer;
};

// A cone from (x, y) along angle: Spectral Strike and Voidflare.
struct Cone
{
    float x, y, angle, length, halfAngle, minHalfWidth;
};

struct Hazards
{
    std::vector<Circle> circles;
    std::vector<Ring> rings;
    std::vector<Cone> cones;

    bool Empty() const { return circles.empty() && rings.empty() && cones.empty(); }
    bool Contains(float x, float y) const;
};

Hazards CollectHazards(Player* bot);

// The nearest reachable point outside every hazard, preferring one that keeps the bot's distance to its
// target. With `inside` only points in that circle count (a soaker stays in the soak, a sheltered bot in
// the shelter). False when there is none in reach.
bool FindSafeSpot(Player* bot, Hazards const& hazards, Position& spot, Circle const* inside = nullptr);

// When no safe spot is in reach: a few yards straight out of the hazard the bot stands in. False when that
// way is blocked too.
bool AwayFromHazard(Player* bot, Hazards const& hazards, Position& spot);

// The circle a bot has to keep to right now (soak or shelter), if any.
bool HoldCircle(Player* bot, Circle& circle);

// Read once per bot every 100 ms: triggers and the multiplier ask for them on every evaluation.
bool InHazard(Player* bot);
// Out of every hazard, but the way to its target runs into one: the bot waits where it is.
bool WayBlocked(Player* bot);
// The soak circle this bot should step into: nobody stands in it yet and this bot is the one to go.
Creature* SoakToTake(Player* bot);
// The shelter, while this bot is not inside it yet.
Creature* ShelterToReach(Player* bot);
// The Holy Crystal in the fight, when this bot is the one to click it.
Creature* FightCrystalToClick(Player* bot);
// Out of combat: the seal crystal this bot should break.
Creature* SealCrystalToBreak(Player* bot);
// Standing in the soak or the shelter, a bot moves for nothing but a hazard.
bool Holding(Player* bot);

// The last spot a bot chose to leave a hazard, so it is not searched again on every tick of the way.
bool KeepRunning(Player* bot, Hazards const& hazards, Position& spot);
void RememberRun(Player* bot, Position const& spot);
}

#endif
