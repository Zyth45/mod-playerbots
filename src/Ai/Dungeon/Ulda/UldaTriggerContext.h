/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_ULDATRIGGERCONTEXT_H
#define PLAYERBOTS_ULDATRIGGERCONTEXT_H

#include "NamedObjectContext.h"
#include "UldaTriggers.h"

class VanillaDungeonUldaTriggerContext : public NamedObjectContext<Trigger>
{
public:
    VanillaDungeonUldaTriggerContext()
    {
        creators["ulda ground tremor"] = &VanillaDungeonUldaTriggerContext::ground_tremor;
    }

private:
    static Trigger* ground_tremor(PlayerbotAI* ai) { return new UldaGroundTremorTrigger(ai); }
};

#endif
