/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_ULDATRIGGERS_H
#define PLAYERBOTS_ULDATRIGGERS_H

#include "DungeonBossHelpers.h"
#include "GenericTriggers.h"
#include "Trigger.h"

// Uldaman (map 70).
namespace Uldaman
{
    // Archaedas's Ground Tremor (375944): an instant stun on everyone within 20 yd, every 15-17 s.
    constexpr float GROUND_TREMOR_RADIUS = 20.0f;

    Unit* ArchaedasNear(PlayerbotAI* botAI, AiObjectContext* context, Player* bot);
}

class UldaGroundTremorTrigger : public Trigger
{
public:
    UldaGroundTremorTrigger(PlayerbotAI* ai) : Trigger(ai, "ulda ground tremor") {}
    bool IsActive() override;
};

#endif
