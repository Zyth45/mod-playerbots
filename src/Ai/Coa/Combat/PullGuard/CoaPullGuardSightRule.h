/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#ifndef COA_PULL_GUARD_SIGHT_RULE_H
#define COA_PULL_GUARD_SIGHT_RULE_H

// The body pull guard in a fight (MovementAction::IsBodyPullRisk), with AiPlayerbot.CoaCombat.PullGuardSight.Enable.
// In a dungeon fight a healer or damage dealer refuses a combat move that ends within an idle pack's aggro distance.
// That also refused the moves back into line of sight: a ranged bot behind a wall, near a pack in the next room, never
// came out and never cast. A mob only aggroes what it sees, so in a fight a pack counts only when it would see the
// destination, and only when the move brings the bot closer to it than it already stands. With
// AiPlayerbot.CoaCombat.PullGuardSight.Strict the second condition goes: a sideways step at the same distance that comes
// into the pack's sight counts too.
namespace CoaPullGuardSightRule
{
    struct Pack
    {
        float destDist = 0.0f;     // pack to the move's destination
        float botDist = 0.0f;      // pack to the bot now
        float aggro = 0.0f;        // the pack's aggro distance against the bot
        float margin = 0.0f;       // 0 in a fight, 3 yd out of it
        bool fighting = false;     // the bot is in combat
        bool sightRuleOn = false;  // AiPlayerbot.CoaCombat.PullGuardSight.Enable
        bool strict = false;       // AiPlayerbot.CoaCombat.PullGuardSight.Strict
    };

    // Whether the pack's line of sight to the destination is needed for the verdict (the costly read, done last).
    inline bool NeedsSight(Pack const& p)
    {
        return p.destDist < p.aggro + p.margin && p.fighting && p.sightRuleOn && (p.strict || p.destDist < p.botDist);
    }

    // Whether this pack makes the move a pull risk. `packSeesDest` is read only when NeedsSight says so.
    inline bool IsRisk(Pack const& p, bool packSeesDest)
    {
        if (p.destDist >= p.aggro + p.margin)
            return false;
        if (!p.fighting || !p.sightRuleOn)
            return true;  // the guard as it was
        if (!p.strict && p.destDist >= p.botDist)
            return false;  // no closer than the bot stands now: this move pulls nothing new
        return packSeesDest;
    }
}

#endif
