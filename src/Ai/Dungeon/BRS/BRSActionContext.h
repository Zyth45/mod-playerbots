/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_BRSACTIONCONTEXT_H
#define PLAYERBOTS_BRSACTIONCONTEXT_H

#include "Action.h"
#include "BRSActions.h"
#include "NamedObjectContext.h"

class VanillaDungeonBRSActionContext : public NamedObjectContext<Action>
{
public:
    VanillaDungeonBRSActionContext()
    {
        creators["brs move from line attack"] = &VanillaDungeonBRSActionContext::move_from_line_attack;
        creators["brs move from bladestorm"] = &VanillaDungeonBRSActionContext::move_from_bladestorm;
        creators["brs move from frontal cone"] = &VanillaDungeonBRSActionContext::move_from_frontal_cone;
    }

private:
    static Action* move_from_line_attack(PlayerbotAI* ai) { return new BrsMoveFromLineAttackAction(ai); }
    static Action* move_from_bladestorm(PlayerbotAI* ai) { return new BrsMoveFromBladestormAction(ai); }
    static Action* move_from_frontal_cone(PlayerbotAI* ai) { return new BrsMoveFromFrontalConeAction(ai); }
};

#endif
