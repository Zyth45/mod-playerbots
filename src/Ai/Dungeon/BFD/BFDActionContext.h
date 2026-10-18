/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_BFDACTIONCONTEXT_H
#define PLAYERBOTS_BFDACTIONCONTEXT_H

#include "Action.h"
#include "BFDActions.h"
#include "NamedObjectContext.h"

class VanillaDungeonBFDActionContext : public NamedObjectContext<Action>
{
public:
    VanillaDungeonBFDActionContext()
    {
        creators["bfd move from torrent"] = &VanillaDungeonBFDActionContext::move_from_torrent;
        creators["bfd move from frontal cone"] = &VanillaDungeonBFDActionContext::move_from_frontal_cone;
    }

private:
    static Action* move_from_torrent(PlayerbotAI* ai) { return new BfdMoveFromTorrentAction(ai); }
    static Action* move_from_frontal_cone(PlayerbotAI* ai) { return new BfdMoveFromFrontalConeAction(ai); }
};

#endif
