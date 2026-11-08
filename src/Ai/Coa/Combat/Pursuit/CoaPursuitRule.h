/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COA_PURSUIT_RULE_H
#define PLAYERBOTS_COA_PURSUIT_RULE_H

#include <cmath>

// No pursuit of a fleeing mob out of the fight (CoA Combat, lot 4; Shadowfang Keep, point c).
//
// A creature at low health runs for help (CreatureFamilyFleeAssistanceRadius 30): it flees, or seeks assistance, then
// comes back with friends. A damage dealer who follows it runs ahead of the group into the next pack, and in a dense
// place like Shadowfang Keep's courtyard that chains the whole keep into the fight. So a non-tank bot does not start a
// chase (stock reach / chase, or a gap-closer) toward a fleeing mob when the chase would END more than LEASH yards from
// its group's tank. Ranged bots keep shooting it from where they stand; the mob comes back, or the tank goes for it.
//
// No game types here, so the unit test builds it alone.
namespace CoaPursuitRule
{
    inline constexpr float LEASH_YARDS = 20.0f;

    // Where a chase toward a target ENDS, as a distance to the tank (2D): a mover with reach `range` stops that far
    // short of the target on its own side, one already inside its reach does not move, range 0 ends on the target.
    inline float ChaseEndToTank(float botX, float botY, float targetX, float targetY, float tankX, float tankY,
                                float range)
    {
        float const dx = botX - targetX;
        float const dy = botY - targetY;
        float const toBot = std::sqrt(dx * dx + dy * dy);
        float endX = targetX;
        float endY = targetY;
        if (toBot <= range)
        {
            endX = botX;
            endY = botY;
        }
        else if (range > 0.0f && toBot > 0.0f)
        {
            endX = targetX + dx / toBot * range;
            endY = targetY + dy / toBot * range;
        }
        return std::sqrt((endX - tankX) * (endX - tankX) + (endY - tankY) * (endY - tankY));
    }

    struct Situation
    {
        bool chaseAction = false;    // a stock reach / chase, or a gap-closer
        bool targetFleeing = false;  // the target runs (fleeing, seeking assistance, timed flee)
        bool hasTank = false;        // the group's tank, alive, on the bot's map
        bool botIsTank = false;
        float chaseEndToTank = 0.0f;
    };

    inline bool Forbids(Situation const& s)
    {
        return s.chaseAction && s.targetFleeing && s.hasTank && !s.botIsTank && s.chaseEndToTank > LEASH_YARDS;
    }
}

#endif
