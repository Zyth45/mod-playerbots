/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_SMTRIGGERS_H
#define PLAYERBOTS_SMTRIGGERS_H

#include "Trigger.h"

class ArcanistDoanBossCastingDetonationTrigger : public Trigger
{
public:
    ArcanistDoanBossCastingDetonationTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "arcanist doan boss casting detonation") {}
    bool IsActive() override;
};

#endif
