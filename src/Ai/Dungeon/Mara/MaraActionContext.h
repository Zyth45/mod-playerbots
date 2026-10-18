/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_MARAACTIONCONTEXT_H
#define PLAYERBOTS_MARAACTIONCONTEXT_H

#include "Action.h"
#include "MaraActions.h"
#include "NamedObjectContext.h"

class VanillaDungeonMaraActionContext : public NamedObjectContext<Action>
{
public:
    VanillaDungeonMaraActionContext()
    {
        creators["mara share boulder"] = &VanillaDungeonMaraActionContext::share_boulder;
        creators["mara move from frontal cone"] = &VanillaDungeonMaraActionContext::move_from_frontal_cone;
        creators["mara move from dust field"] = &VanillaDungeonMaraActionContext::move_from_dust_field;
    }

private:
    static Action* share_boulder(PlayerbotAI* ai) { return new MaraShareBoulderAction(ai); }
    static Action* move_from_frontal_cone(PlayerbotAI* ai) { return new MaraMoveFromFrontalConeAction(ai); }
    static Action* move_from_dust_field(PlayerbotAI* ai) { return new MaraMoveFromDustFieldAction(ai); }
};

#endif
