/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

/*
 * Boss knowledge: what an enemy is about to do to the ground around it.
 *
 * Two layers. Classify() reads any spell from the server's own spell data (point blank, cone,
 * area at a spot, ground effect, chain) and so works for every creature, scripted in C++ or not,
 * the moment its cast starts. The table built by Load() adds what a creature can cast before it
 * does: the casts its smart_scripts rows hold (timed action lists included), read once at
 * startup. Encounters written in C++ have no rows; they still get the live layer.
 *
 * Upstream "avoid aoe" only acts once a bot already stands in a ground effect. "coa boss dodge"
 * acts on the cast itself: a melee damage dealer steps out of a point blank cast or from in
 * front of a cone, anyone leaves the spot of an area cast, and ranged members keep out of the
 * reach of an instant point blank spell the table knows.
 */

#ifndef PLAYERBOTS_COABOSSKNOWLEDGE_H
#define PLAYERBOTS_COABOSSKNOWLEDGE_H

#include "Define.h"

#include <vector>

class SpellInfo;

namespace CoaBossKnowledge
{
enum class Shape : uint8
{
    None,
    PointBlank,  // around the caster
    Cone,        // in front of the caster
    AtDest,      // around a spot or a target away from the caster
    Ground,      // a persistent area left on the ground
    Chain        // jumps from one target to the next
};

struct Danger
{
    Shape shape = Shape::None;
    float radius = 0.0f;
    float arc = 0.0f;  // cone width in radians
};

Danger Classify(SpellInfo const* info);

// Read once at startup (OnBeforeWorldInitialized); later calls do nothing.
void Load();

// Spells this creature entry casts by its smart_scripts rows, or nullptr.
std::vector<uint32> const* KnownSpells(uint32 creatureEntry);

// The widest instant point blank spell of this entry (0 when none): no cast bar to react to.
float InstantPointBlankRadius(uint32 creatureEntry);
}

#endif
