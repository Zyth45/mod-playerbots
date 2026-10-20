/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_SMTRIGGERCONTEXT_H
#define PLAYERBOTS_SMTRIGGERCONTEXT_H

#include "NamedObjectContext.h"
#include "SMTriggers.h"

class VanillaDungeonSMTriggerContext : public NamedObjectContext<Trigger>
{
public:
    VanillaDungeonSMTriggerContext()
    {
        creators["arcanist doan boss casting detonation"] =
            &VanillaDungeonSMTriggerContext::arcanist_doan_boss_casting_detonation;
    }
private:
    static Trigger* arcanist_doan_boss_casting_detonation(PlayerbotAI* botAI) {
        return new ArcanistDoanBossCastingDetonationTrigger(botAI);
    }
};

#endif
