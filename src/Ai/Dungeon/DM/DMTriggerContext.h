/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_DMTRIGGERCONTEXT_H
#define PLAYERBOTS_DMTRIGGERCONTEXT_H

#include "DMTriggers.h"
#include "NamedObjectContext.h"

class VanillaDungeonDMTriggerContext : public NamedObjectContext<Trigger>
{
public:
    VanillaDungeonDMTriggerContext()
    {
        creators["dm massive geyser"] = &VanillaDungeonDMTriggerContext::massive_geyser;
        creators["dm geyser spread"] = &VanillaDungeonDMTriggerContext::geyser_spread;
        creators["dm hydrolings"] = &VanillaDungeonDMTriggerContext::hydrolings;
    }

private:
    static Trigger* massive_geyser(PlayerbotAI* ai) { return new DmMassiveGeyserTrigger(ai); }
    static Trigger* geyser_spread(PlayerbotAI* ai) { return new DmGeyserSpreadTrigger(ai); }
    static Trigger* hydrolings(PlayerbotAI* ai) { return new DmHydrolingsTrigger(ai); }
};

#endif
