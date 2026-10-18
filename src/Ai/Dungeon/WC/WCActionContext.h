/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_WCACTIONCONTEXT_H
#define PLAYERBOTS_WCACTIONCONTEXT_H

#include "Action.h"
#include "NamedObjectContext.h"
#include "WCActions.h"

class VanillaDungeonWCActionContext : public NamedObjectContext<Action>
{
public:
    VanillaDungeonWCActionContext()
    {
        creators["wc move from overrun"] = &VanillaDungeonWCActionContext::move_from_overrun;
    }

private:
    static Action* move_from_overrun(PlayerbotAI* ai) { return new WcMoveFromOverrunAction(ai); }
};

#endif
