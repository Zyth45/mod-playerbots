/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_STRATACTIONCONTEXT_H
#define PLAYERBOTS_STRATACTIONCONTEXT_H

#include "Action.h"
#include "NamedObjectContext.h"
#include "StratActions.h"

class VanillaDungeonStratActionContext : public NamedObjectContext<Action>
{
public:
    VanillaDungeonStratActionContext()
    {
        creators["strat move from crusader's hammer"] = &VanillaDungeonStratActionContext::move_from_hammer;
        creators["strat move from frontal cone"] = &VanillaDungeonStratActionContext::move_from_frontal_cone;
        creators["strat move from divine retribution"] = &VanillaDungeonStratActionContext::move_from_retribution;
    }

private:
    static Action* move_from_hammer(PlayerbotAI* ai) { return new StratMoveFromCrusadersHammerAction(ai); }
    static Action* move_from_frontal_cone(PlayerbotAI* ai) { return new StratMoveFromFrontalConeAction(ai); }
    static Action* move_from_retribution(PlayerbotAI* ai) { return new StratMoveFromRetributionAction(ai); }
};

#endif
