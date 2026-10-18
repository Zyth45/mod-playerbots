/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_RFDTRIGGERS_H
#define PLAYERBOTS_RFDTRIGGERS_H

#include "DungeonBossHelpers.h"
#include "GenericTriggers.h"
#include "Trigger.h"

// Razorfen Downs (map 129).
namespace RazorfenDowns
{
    // Glutton's Expel Bloat (2100718): an 8 s cast that hits everyone within 13 yd for a tank's worth of health.
    constexpr float EXPEL_BLOAT_RADIUS = 13.0f;

    // Mordresh Fire Eye's Bone Storm (2100704): for 8 s he hits everyone within 8 yd every second (2100703) and
    // does not melee.
    constexpr float BONE_STORM_RADIUS = 8.0f;

    // Amnennar the Coldbringer marks a player before a spell lands on them and the players around: Steal Warmth
    // (2100709, 5 yd) after the 5 yard swirly, Ice Spikes (2100713, 3 yd) after the 3 yard one.
    struct Mark
    {
        char const* aura;
        float radius;
    };

    constexpr Mark AMNENNAR_MARKS[] = {
        { "5 yard Blue swirly on target", 5.0f },
        { "3 yard Blue swirly on target", 3.0f },
    };

    Unit* GluttonExpelling(PlayerbotAI* botAI, AiObjectContext* context);
    Unit* MordreshStorming(PlayerbotAI* botAI, AiObjectContext* context);

    // Another group member marked by Amnennar that the bot stands too close to, nullptr if none.
    Player* MarkedNear(Player* bot, float& radius);
}

class RfdGluttonExpelBloatTrigger : public Trigger
{
public:
    RfdGluttonExpelBloatTrigger(PlayerbotAI* ai) : Trigger(ai, "rfd glutton expel bloat") {}
    bool IsActive() override;
};

class RfdMordreshBoneStormTrigger : public Trigger
{
public:
    RfdMordreshBoneStormTrigger(PlayerbotAI* ai) : Trigger(ai, "rfd mordresh bone storm") {}
    bool IsActive() override;
};

class RfdAmnennarMarkTrigger : public Trigger
{
public:
    RfdAmnennarMarkTrigger(PlayerbotAI* ai) : Trigger(ai, "rfd amnennar mark") {}
    bool IsActive() override;
};

#endif
