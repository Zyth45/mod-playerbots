/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_SCHOLOACTIONCONTEXT_H
#define PLAYERBOTS_SCHOLOACTIONCONTEXT_H

#include "Action.h"
#include "NamedObjectContext.h"
#include "ScholoActions.h"

class VanillaDungeonScholoActionContext : public NamedObjectContext<Action>
{
public:
    VanillaDungeonScholoActionContext()
    {
        creators["scholo move from frontal cone"] = &VanillaDungeonScholoActionContext::move_from_frontal_cone;
    }

private:
    static Action* move_from_frontal_cone(PlayerbotAI* ai) { return new ScholoMoveFromFrontalConeAction(ai); }
};

#endif
