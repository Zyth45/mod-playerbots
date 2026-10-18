/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_ULDAACTIONCONTEXT_H
#define PLAYERBOTS_ULDAACTIONCONTEXT_H

#include "Action.h"
#include "NamedObjectContext.h"
#include "UldaActions.h"

class VanillaDungeonUldaActionContext : public NamedObjectContext<Action>
{
public:
    VanillaDungeonUldaActionContext()
    {
        creators["ulda move from ground tremor"] = &VanillaDungeonUldaActionContext::move_from_ground_tremor;
    }

private:
    static Action* move_from_ground_tremor(PlayerbotAI* ai) { return new UldaMoveFromGroundTremorAction(ai); }
};

#endif
