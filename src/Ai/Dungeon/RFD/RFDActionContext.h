/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_RFDACTIONCONTEXT_H
#define PLAYERBOTS_RFDACTIONCONTEXT_H

#include "Action.h"
#include "NamedObjectContext.h"
#include "RFDActions.h"

class VanillaDungeonRFDActionContext : public NamedObjectContext<Action>
{
public:
    VanillaDungeonRFDActionContext()
    {
        creators["rfd move from expel bloat"] = &VanillaDungeonRFDActionContext::move_from_expel_bloat;
        creators["rfd move from bone storm"] = &VanillaDungeonRFDActionContext::move_from_bone_storm;
        creators["rfd move from marked player"] = &VanillaDungeonRFDActionContext::move_from_marked_player;
    }

private:
    static Action* move_from_expel_bloat(PlayerbotAI* ai) { return new RfdMoveFromExpelBloatAction(ai); }
    static Action* move_from_bone_storm(PlayerbotAI* ai) { return new RfdMoveFromBoneStormAction(ai); }
    static Action* move_from_marked_player(PlayerbotAI* ai) { return new RfdMoveFromMarkedAction(ai); }
};

#endif
