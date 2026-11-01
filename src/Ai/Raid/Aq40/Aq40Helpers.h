/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AQ40HELPERS_H
#define PLAYERBOTS_AQ40HELPERS_H

#include "Common.h"
#include "Position.h"
#include <vector>

class Creature;
class Player;
class Unit;

// Temple of Ahn'Qiraj as Conquest of Azeroth runs it (mod-coa-raid-difficulty,
// boss_cthun_coa.cpp). Only C'Thun is handled: the CoA Twin Emperors are a
// different fight from stock and are left to the generic strategies.
namespace Aq40Helpers
{
inline constexpr uint32 AQ40_MAP_ID = 531;

// Instance data ids (temple_of_ahnqiraj.h)
inline constexpr uint32 DATA_CTHUN = 9;
inline constexpr uint32 DATA_EYE_OF_CTHUN = 18;

enum Aq40Npcs : uint32
{
    NPC_EYE_OF_CTHUN = 15589,
    NPC_CTHUN = 15727,
    NPC_CLAW_TENTACLE = 15725,        // CoA: Manipulator, Devastator or Malignant, same entry
    NPC_EYE_TENTACLE = 15726,         // CoA: Miasma, Eradicate or Consume, same entry
    NPC_GIANT_EYE_TENTACLE = 15334,
    NPC_GIANT_CLAW_TENTACLE = 15728,
    NPC_FLESH_TENTACLE = 15802,
};

enum Aq40Spells : uint32
{
    SPELL_DARK_GLARE = 26029,                    // stock, kept by CoA
    SPELL_RED_COLORATION = 22518,                // on the Eye for the whole glare
    SPELL_DIGESTIVE_ACID = 26476,                // in the stomach
    SPELL_ELDRITCH_REVELATIONS = 2117015,        // Eye, 3s cast, then 3 pulses: facing it stuns 8s
    SPELL_LESSER_ELDRITCH_REVELATIONS = 2117079, // Giant Eye Tentacle, 3s cast, 40 yd
    SPELL_FORCE_SHIELD = 2117102,                // phase 2, -99% damage taken
    SPELL_WEAKENING = 2117107,                   // both Flesh Tentacles dead, 30s
    SPELL_DEVOURS = 2117117,                     // the mouth hunts this player
};

// Area triggers a client fires and a server bot has to send itself
inline constexpr uint32 AT_CTHUN_STOMACH_EXIT = 4033;
inline constexpr uint32 AT_CTHUN_CENTER = 4036;

// The Eye and C'Thun share this center (creature spawns 15589/15727)
inline Position const CTHUN_CENTER{-8578.8f, 1986.2f, 100.7f};
inline constexpr float CTHUN_ROOM_RADIUS = 140.0f;
inline constexpr float CTHUN_SPIT_OUT_RADIUS = 6.5f;  // area trigger 4036 is 7 yd

// CoA Eye Beam and Lesser Eye Beam leap to the nearest player within 12 yd
// (IsWithinDistInMap, so 15 yd center to center between two players), five
// times, +15% each. Every spot below is chosen with that in mind.
inline constexpr float BEAM_CHAIN_DISTANCE = 15.0f;
// The mouth of From Beneath You It Devours swallows its prey and everyone
// within 8 yd of it.
inline constexpr float DEVOUR_SWALLOW_RANGE = 8.0f;
inline constexpr float DEVOUR_CLEAR_DISTANCE = 12.0f;

// Phase 2 burn: C'Thun is hit from about 10 yd of his center; melee stand at
// 8 yd, outside the 7 yd spit-out circle. Ranged step in to this radius.
inline constexpr float CTHUN_BURN_MELEE_RADIUS = 8.0f;
inline constexpr float CTHUN_BURN_RANGED_RADIUS = 27.0f;

inline constexpr float CTHUN_MELEE_LEASH = 10.0f;    // melee leave their spot only for a unit this close
inline constexpr float CTHUN_RANGED_REACH = 29.0f;   // 30 yd spells, edge to edge
inline constexpr float CTHUN_FAR_CLAW = 20.0f;       // a Giant Claw farther from a bot's spot is left alone

inline constexpr float GLARE_HALF_WIDTH = 8.0f;      // the glare hits within 5 yd of its line, plus a margin
inline constexpr int GLARE_LOOKAHEAD_TICKS = 3;

bool IsInAq40(Player* bot);
Creature* GetEye(Player* bot);
Creature* GetCthun(Player* bot);
bool IsCthunEncounter(Player* bot);
bool IsCthunPhaseTwo(Player* bot);
bool IsCthunWeakened(Player* bot);
bool InStomach(Unit const* unit);
bool IsCthunBody(Unit const* unit);

// The living units of `entry` near the bot: the room, or the stomach when the
// bot is in it. The grid search runs at most twice a second per instance.
std::vector<Creature*> GetCthunUnits(Player* bot, uint32 entry);

// The bot's spot on the three rings around the Eye, dealt by role and GUID so
// every bot of the group computes the same table without sharing state.
bool GetCthunSpot(Player* bot, Position& spot);
bool IsEyeMeleeSpot(Player* bot);

// Dark Glare: the sweep direction seen by this bot (0 while unknown) and
// whether the bot stands in the band now or within the next ticks.
float GetGlareDirection(Player* bot, Unit* eye);
bool IsInGlarePath(Player* bot, Unit* eye);
Position GetGlareDodge(Player* bot, Unit* eye);

// Eldritch Revelations: the caster the bot must look away from, if any. The
// window lasts from the cast until its last pulse (2s after the cast ends).
Unit* GetEldritchCaster(Player* bot);

bool ShouldLeaveStomach(Player* bot);
Unit* ChooseCthunTarget(Player* bot);
Player* GetDevourPreyNear(Player* bot);
}  // namespace Aq40Helpers

#endif
