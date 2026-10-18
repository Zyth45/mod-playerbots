/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_DUNGEONBOSSHELPERS_H
#define PLAYERBOTS_DUNGEONBOSSHELPERS_H

#include "MovementActions.h"

#include <cmath>
#include <span>

class Action;
class AiObjectContext;
class Player;
class PlayerbotAI;
class Unit;

// Shared by the CoA dungeon strategies. CoA runs the same boss kits on Normal, Heroic and Mythic; only health and
// damage change, and the Heroic/Mythic spells keep their names, so spells are matched by name.
namespace DungeonBoss
{
    constexpr float DegreesToRadians(float degrees) { return degrees * float(M_PI) / 180.0f; }

    bool HasAuraNamed(Unit const* unit, char const* name);
    // Milliseconds left on the unit's aura of that name, -1 without one.
    int32 AuraRemainingNamed(Unit const* unit, char const* name);
    bool IsCastingNamed(Unit const* unit, char const* name);

    // The unit's angle around `center`, measured from the center's facing, in (-pi, pi].
    float RelativeAngle(Unit const* center, Unit const* unit);

    // A cone a boss casts in front of itself. With a spell name the cone is there while the boss casts it or
    // carries it as an aura (a channel such as Goblin Dragon Gun); without one the boss casts it so often, or
    // without a cast bar, that its front is never safe.
    struct FrontalCone
    {
        char const* boss;
        float reach;
        float halfArc;
        char const* spell;
    };

    // Margin around a cone, so that the boss turning to a new target does not catch a bot at its edge.
    constexpr float CONE_MARGIN = DegreesToRadians(15.0f);
    constexpr float CONE_SIDE_STEP = DegreesToRadians(30.0f);

    // In front of the unit, within `reach` of its edge and `halfArc` (plus the margin) of its facing.
    bool InFront(Unit const* unit, Player* bot, float reach, float halfArc);

    // The boss whose frontal cone the bot stands in, nullptr if none. The cone's own target, the boss's victim or the
    // unit the named cast is aimed at, is not counted.
    Unit* ConeOver(PlayerbotAI* botAI, AiObjectContext* context, Player* bot,
                   std::span<FrontalCone const> cones, FrontalCone const*& cone);

    // Within `radius` of the unit's edge, plus a margin.
    bool Within(Player* bot, Unit const* unit, float radius);
    float KeepAwayDistance(Unit const* unit, float radius);

    // The unit the caster's current cast of that spell is aimed at, nullptr if it casts no such spell at a unit.
    Unit* CastTarget(Unit const* caster, char const* spell);

    // On the strip `halfWidth` wide that runs `length` yards from `source` in the direction `angle`.
    bool InPath(Unit const* source, float angle, float length, float halfWidth, Unit const* unit);

    // Whether the action walks the bot somewhere other than out of danger: following, formation, reaching melee
    // or spell range. A strategy's own escape moves, "avoid aoe" and "coa boss dodge" are not counted.
    bool MovesIntoDanger(Action* action);
}

class DungeonBossMovementAction : public MovementAction
{
public:
    DungeonBossMovementAction(PlayerbotAI* ai, std::string const name) : MovementAction(ai, name) {}

protected:
    // To `angle` from the boss's facing, `distance` yards from it.
    bool MoveAround(Unit* boss, float angle, float distance);
    bool MoveOutOfCone(Unit* boss, DungeonBoss::FrontalCone const& cone);
    bool KeepAway(Unit* unit, float radius);
    // Straight out to the nearer side of the strip of InPath, keeping the bot's place along it.
    bool MoveOutOfPath(Unit* source, float angle, float halfWidth, float step);

private:
    bool MoveToSpot(float x, float y);
};

#endif
