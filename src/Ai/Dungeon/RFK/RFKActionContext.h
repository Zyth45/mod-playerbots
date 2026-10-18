/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_RFKACTIONCONTEXT_H
#define PLAYERBOTS_RFKACTIONCONTEXT_H

#include "Action.h"
#include "NamedObjectContext.h"
#include "RFKActions.h"

class VanillaDungeonRFKActionContext : public NamedObjectContext<Action>
{
public:
    VanillaDungeonRFKActionContext()
    {
        creators["rfk move from frontal cone"] = &VanillaDungeonRFKActionContext::move_from_frontal_cone;
    }

private:
    static Action* move_from_frontal_cone(PlayerbotAI* ai) { return new RfkMoveFromFrontalConeAction(ai); }
};

#endif
