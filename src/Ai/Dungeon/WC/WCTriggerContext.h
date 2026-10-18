/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_WCTRIGGERCONTEXT_H
#define PLAYERBOTS_WCTRIGGERCONTEXT_H

#include "NamedObjectContext.h"
#include "WCTriggers.h"

class VanillaDungeonWCTriggerContext : public NamedObjectContext<Trigger>
{
public:
    VanillaDungeonWCTriggerContext()
    {
        creators["wc skum overrun"] = &VanillaDungeonWCTriggerContext::skum_overrun;
    }

private:
    static Trigger* skum_overrun(PlayerbotAI* ai) { return new WcSkumOverrunTrigger(ai); }
};

#endif
