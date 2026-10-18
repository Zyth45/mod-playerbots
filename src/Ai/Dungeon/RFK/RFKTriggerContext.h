/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_RFKTRIGGERCONTEXT_H
#define PLAYERBOTS_RFKTRIGGERCONTEXT_H

#include "NamedObjectContext.h"
#include "RFKTriggers.h"

class VanillaDungeonRFKTriggerContext : public NamedObjectContext<Trigger>
{
public:
    VanillaDungeonRFKTriggerContext()
    {
        creators["rfk frontal cone"] = &VanillaDungeonRFKTriggerContext::frontal_cone;
    }

private:
    static Trigger* frontal_cone(PlayerbotAI* ai) { return new RfkFrontalConeTrigger(ai); }
};

#endif
