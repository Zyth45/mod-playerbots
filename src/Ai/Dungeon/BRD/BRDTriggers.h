/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_BRDTRIGGERS_H
#define PLAYERBOTS_BRDTRIGGERS_H

#include "DungeonBossHelpers.h"
#include "GenericTriggers.h"
#include "Trigger.h"

// Blackrock Depths (map 230).
namespace BlackrockDepths
{
    using DungeonBoss::DegreesToRadians;

    // While Magmus fights, the Ironhand Guardians along the Iron Hall breathe Gout of Flame (15529) for 10 s every
    // 13-19 s: a 24 degree cone of 25 yd in front of each statue (15538), which never turns.
    constexpr uint32 NPC_IRONHAND_GUARDIAN = 8982;
    constexpr float GOUT_OF_FLAME_REACH = 25.0f;
    constexpr float GOUT_OF_FLAME_HALF_ARC = DegreesToRadians(12.0f);
    constexpr float IRONHAND_SEARCH_RANGE = 40.0f;

    // Ambassador Flamelash calls Burning Spirits from the runes around his platform; each that reaches him dies
    // and makes him stronger (Burning Spirit, 14744, stacking).
    constexpr uint32 NPC_BURNING_SPIRIT = 9178;
    constexpr float BURNING_SPIRIT_SEARCH_RANGE = 60.0f;

    // Lord Roccor (375944) and Grizzle (6524) stun everyone within 20 yd with Ground Tremor, without a cast bar.
    constexpr float GROUND_TREMOR_RADIUS = 20.0f;
    constexpr char const* GROUND_TREMOR_BOSSES[] = { "lord roccor", "grizzle" };

    // Seeth'rel's Cone of Cold (15244): instant, every 15-17 s at the tank, 10 yd and 24 degrees.
    constexpr DungeonBoss::FrontalCone FRONTAL_CONES[] = {
        { "seeth'rel", 10.0f, DegreesToRadians(12.0f), nullptr },
    };

    // An Ironhand Guardian breathing fire at the bot, nullptr if none.
    Creature* GoutOver(Player* bot);

    // The Burning Spirit nearest to Flamelash, nullptr when none is up.
    Creature* BurningSpiritToKill(PlayerbotAI* botAI, AiObjectContext* context, Player* bot);

    // Thaurissan while Moira still fights at his side and heals him: he dies first, and she stops with him.
    Unit* ThaurissanToFocus(PlayerbotAI* botAI, AiObjectContext* context);

    Unit* GroundTremorNear(PlayerbotAI* botAI, AiObjectContext* context, Player* bot);
}

class BrdIronhandGoutTrigger : public Trigger
{
public:
    BrdIronhandGoutTrigger(PlayerbotAI* ai) : Trigger(ai, "brd ironhand gout") {}
    bool IsActive() override;
};

class BrdBurningSpiritTrigger : public Trigger
{
public:
    BrdBurningSpiritTrigger(PlayerbotAI* ai) : Trigger(ai, "brd burning spirit") {}
    bool IsActive() override;
};

class BrdThaurissanFocusTrigger : public Trigger
{
public:
    BrdThaurissanFocusTrigger(PlayerbotAI* ai) : Trigger(ai, "brd thaurissan focus") {}
    bool IsActive() override;
};

class BrdGroundTremorTrigger : public Trigger
{
public:
    BrdGroundTremorTrigger(PlayerbotAI* ai) : Trigger(ai, "brd ground tremor") {}
    bool IsActive() override;
};

class BrdFrontalConeTrigger : public Trigger
{
public:
    BrdFrontalConeTrigger(PlayerbotAI* ai) : Trigger(ai, "brd frontal cone") {}
    bool IsActive() override;
};

#endif
