/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_SFKTRIGGERS_H
#define PLAYERBOTS_SFKTRIGGERS_H

#include "DungeonStrategyUtils.h"
#include "GenericTriggers.h"
#include "PlayerbotAIConfig.h"
#include "Trigger.h"

enum ShadowfangKeepIDs
{
    NPC_WOLF_MASTER_NANDOS         = 3927,
    NPC_ARCHMAGE_ARUGAL            = 4275,

    NPC_BLEAK_WORG                 = 3861,
    NPC_SLAVERING_WORG             = 3862,
    NPC_LUPINE_HORROR              = 3863,
    NPC_LUPINE_DELUSION            = 5097,

    // Arugal's only area spell: a five yard stun around himself.
    SPELL_THUNDERSHOCK             = 7803
};

// Thundershock reaches five yards from Arugal and stuns for five seconds. The extra yards keep a
// ranged bot out of it after he lands from a Shadow Port next to it.
constexpr float THUNDERSHOCK_RADIUS = 5.0f;
constexpr float THUNDERSHOCK_SAFETY = 3.0f;

// Nandos keeps a Lupine Horror and a Bleak Worg within twelve yards and a Slavering Worg at
// twenty-eight; his Call Lupine Horror and Call Bleak Worg put the summons at his feet. The
// distance is measured in three dimensions because the tower stacks packs of the same creatures
// on the floors below him.
constexpr float SFK_NANDOS_PACK_RANGE = 30.0f;

namespace NandosPack
{
    Unit* Nearest(PlayerbotAI* botAI, Player* bot, AiObjectContext* context);
}

class NandosPackTrigger : public Trigger
{
public:
    NandosPackTrigger(PlayerbotAI* ai) : Trigger(ai, "nandos pack") {}
    bool IsActive() override;
};

class ArugalThundershockTrigger : public Trigger
{
public:
    ArugalThundershockTrigger(PlayerbotAI* ai) : Trigger(ai, "arugal thundershock") {}
    bool IsActive() override;
};

#endif
