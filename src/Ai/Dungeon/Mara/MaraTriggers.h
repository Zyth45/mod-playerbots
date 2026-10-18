/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_MARATRIGGERS_H
#define PLAYERBOTS_MARATRIGGERS_H

#include "DungeonBossHelpers.h"
#include "GenericTriggers.h"
#include "Trigger.h"

// Maraudon (map 349).
namespace Maraudon
{
    using DungeonBoss::DegreesToRadians;

    // Landslide's Hurl Boulder (2100732): a 3 s cast at the tank that drops Gigantic Boulder (2100733) on everyone
    // within 10 yd of them, its damage split between all of them (spell_custom_attr SHARE_DAMAGE).
    constexpr float BOULDER_RADIUS = 10.0f;
    constexpr float BOULDER_GATHER_DISTANCE = 6.0f;
    constexpr float BOULDER_GATHER_BEHIND = 4.0f;
    constexpr float BOULDER_GATHER_RANGE = 40.0f;

    // Lord Vyletongue's Focused Burst (2100740): a 4.5 s cast at the tank, then a 24 degree cone of 100 yd at them
    // (2100741). Tinkerer Gizlock's Goblin Dragon Gun (21833): 8 s of a 24 degree cone of 10 yd in front of him.
    constexpr DungeonBoss::FrontalCone FRONTAL_CONES[] = {
        { "lord vyletongue", 100.0f, DegreesToRadians(12.0f), "Focused Burst" },
        { "tinkerer gizlock", 10.0f, DegreesToRadians(12.0f), "Goblin Dragon Gun" },
    };

    // Princess Theradras's Dust Field (21909): 8 s in which she hits and knocks back everyone within 15 yd every
    // second (21868).
    constexpr float DUST_FIELD_RADIUS = 15.0f;

    // The player Landslide hurls his boulder at, while the bot should come to share it; nullptr otherwise.
    Unit* BoulderToShare(PlayerbotAI* botAI, AiObjectContext* context, Player* bot, Unit*& landslide);

    Unit* TheradrasDusting(PlayerbotAI* botAI, AiObjectContext* context);
}

class MaraHurlBoulderTrigger : public Trigger
{
public:
    MaraHurlBoulderTrigger(PlayerbotAI* ai) : Trigger(ai, "mara hurl boulder") {}
    bool IsActive() override;
};

class MaraFrontalConeTrigger : public Trigger
{
public:
    MaraFrontalConeTrigger(PlayerbotAI* ai) : Trigger(ai, "mara frontal cone") {}
    bool IsActive() override;
};

class MaraDustFieldTrigger : public Trigger
{
public:
    MaraDustFieldTrigger(PlayerbotAI* ai) : Trigger(ai, "mara dust field") {}
    bool IsActive() override;
};

#endif
