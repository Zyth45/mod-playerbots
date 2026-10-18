/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_SCHOLOTRIGGERCONTEXT_H
#define PLAYERBOTS_SCHOLOTRIGGERCONTEXT_H

#include "NamedObjectContext.h"
#include "ScholoTriggers.h"

class VanillaDungeonScholoTriggerContext : public NamedObjectContext<Trigger>
{
public:
    VanillaDungeonScholoTriggerContext()
    {
        creators["scholo frontal cone"] = &VanillaDungeonScholoTriggerContext::frontal_cone;
    }

private:
    static Trigger* frontal_cone(PlayerbotAI* ai) { return new ScholoFrontalConeTrigger(ai); }
};

#endif
