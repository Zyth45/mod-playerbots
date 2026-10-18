/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_BRSTRIGGERS_H
#define PLAYERBOTS_BRSTRIGGERS_H

#include "DungeonBossHelpers.h"
#include "GenericTriggers.h"
#include "Trigger.h"

// Blackrock Spire (map 229), lower and upper.
namespace BlackrockSpire
{
    using DungeonBoss::DegreesToRadians;

    // A cast at one player that then sweeps the line from the boss to them.
    struct LineAttack
    {
        char const* boss;
        char const* spell;
        float length;  // 0: up to the target
        float radius;
        bool fromBossBody;
    };

    // General Drakkisath's Heatwave (2102203, a 3 s cast at the tank): 14 patches of fire 1 yd wide, laid from
    // 3 to 29 yd towards the tank (boss_drakkisath). The Beast's Overrun (2102159, a 4 s cast at a random player):
    // he charges to them and tramples everyone within 4 yd of himself on the way (2102160, boss_the_beast).
    constexpr LineAttack LINE_ATTACKS[] = {
        { "general drakkisath", "Heatwave", 30.0f, 1.0f, false },
        { "the beast", "Overrun", 0.0f, 4.0f, true },
    };

    constexpr float LINE_MARGIN = 1.5f;
    constexpr float LINE_SIDE_STEP = 3.0f;

    // Warchief Rend Blackhand's Bladestorm (2102847): 10 s in which he hits everyone within 5 yd every second.
    constexpr float BLADESTORM_RADIUS = 5.0f;

    // Gyth breathes Flame Breath (16390), Freeze (16350) and Corrosive Acid Breath (16359), the last two without a
    // cast bar: 24 degree cones of 20 yd at the tank.
    constexpr DungeonBoss::FrontalCone FRONTAL_CONES[] = {
        { "gyth", 20.0f, DegreesToRadians(12.0f), nullptr },
    };

    // The line attack about to sweep the bot, with the boss, the direction and the half width; nullptr if none.
    // The player the attack is aimed at cannot leave it and is not counted.
    Unit* LineOver(PlayerbotAI* botAI, AiObjectContext* context, Player* bot, float& angle, float& halfWidth);

    Unit* RendStorming(PlayerbotAI* botAI, AiObjectContext* context);
}

class BrsLineAttackTrigger : public Trigger
{
public:
    BrsLineAttackTrigger(PlayerbotAI* ai) : Trigger(ai, "brs line attack") {}
    bool IsActive() override;
};

class BrsRendBladestormTrigger : public Trigger
{
public:
    BrsRendBladestormTrigger(PlayerbotAI* ai) : Trigger(ai, "brs rend bladestorm") {}
    bool IsActive() override;
};

class BrsFrontalConeTrigger : public Trigger
{
public:
    BrsFrontalConeTrigger(PlayerbotAI* ai) : Trigger(ai, "brs frontal cone") {}
    bool IsActive() override;
};

#endif
