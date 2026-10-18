/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_STRATTRIGGERS_H
#define PLAYERBOTS_STRATTRIGGERS_H

#include "DungeonBossHelpers.h"
#include "GenericTriggers.h"
#include "Trigger.h"

// Stratholme (map 329), both sides.
namespace Stratholme
{
    using DungeonBoss::DegreesToRadians;

    // Grand Crusader Dathrohan's Crusader's Hammer (17286): an instant stun on everyone within 20 yd.
    constexpr float CRUSADERS_HAMMER_RADIUS = 20.0f;

    // Balnazzar's Carrion Swarm (92573): a 1 s cast every 8-9 s, a 24 degree cone of 40 yd in front of him.
    constexpr DungeonBoss::FrontalCone FRONTAL_CONES[] = {
        { "balnazzar", 40.0f, DegreesToRadians(12.0f), nullptr },
    };

    // Timmy the Cruel's Divine Retribution (680624) stores a quarter of what he hits the tank for during 8 s, then
    // releases it on everyone within 10 yd of the tank (570147, spell_coa_divine_retribution).
    constexpr float DIVINE_RETRIBUTION_RADIUS = 10.0f;
    constexpr int32 DIVINE_RETRIBUTION_WARNING_MS = 2500;

    Unit* DathrohanNear(PlayerbotAI* botAI, AiObjectContext* context, Player* bot);

    // A group member whose Divine Retribution is about to release, and the bot is too close to; nullptr if none.
    Player* RetributionNear(PlayerbotAI* botAI, AiObjectContext* context, Player* bot);
}

class StratCrusadersHammerTrigger : public Trigger
{
public:
    StratCrusadersHammerTrigger(PlayerbotAI* ai) : Trigger(ai, "strat crusader's hammer") {}
    bool IsActive() override;
};

class StratFrontalConeTrigger : public Trigger
{
public:
    StratFrontalConeTrigger(PlayerbotAI* ai) : Trigger(ai, "strat frontal cone") {}
    bool IsActive() override;
};

class StratDivineRetributionTrigger : public Trigger
{
public:
    StratDivineRetributionTrigger(PlayerbotAI* ai) : Trigger(ai, "strat divine retribution") {}
    bool IsActive() override;
};

#endif
