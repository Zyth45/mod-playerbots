/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_DMACTIONCONTEXT_H
#define PLAYERBOTS_DMACTIONCONTEXT_H

#include "Action.h"
#include "DMActions.h"
#include "NamedObjectContext.h"

class VanillaDungeonDMActionContext : public NamedObjectContext<Action>
{
public:
    VanillaDungeonDMActionContext()
    {
        creators["dm leave massive geyser"] = &VanillaDungeonDMActionContext::leave_massive_geyser;
        creators["dm geyser spread"] = &VanillaDungeonDMActionContext::geyser_spread;
        creators["dm attack hydroling"] = &VanillaDungeonDMActionContext::attack_hydroling;
    }

private:
    static Action* leave_massive_geyser(PlayerbotAI* ai) { return new DmLeaveMassiveGeyserAction(ai); }
    static Action* geyser_spread(PlayerbotAI* ai) { return new DmGeyserSpreadAction(ai); }
    static Action* attack_hydroling(PlayerbotAI* ai) { return new DmAttackHydrolingAction(ai); }
};

#endif
