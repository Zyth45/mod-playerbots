/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_SMACTIONCONTEXT_H
#define PLAYERBOTS_SMACTIONCONTEXT_H

#include "Action.h"
#include "NamedObjectContext.h"
#include "SMActions.h"

class VanillaDungeonSMActionContext : public NamedObjectContext<Action>
{
public:
    VanillaDungeonSMActionContext()
    {
        creators["sm move from doan"] = &VanillaDungeonSMActionContext::move_from_doan;
        creators["sm doan ranged keep away"] = &VanillaDungeonSMActionContext::doan_ranged_keep_away;
        creators["sm move from herod"] = &VanillaDungeonSMActionContext::move_from_herod;
        creators["sm attack whitemane"] = &VanillaDungeonSMActionContext::attack_whitemane;
    }

private:
    static Action* move_from_doan(PlayerbotAI* ai) { return new SmMoveFromDoanAction(ai); }
    static Action* doan_ranged_keep_away(PlayerbotAI* ai) { return new SmDoanRangedAction(ai); }
    static Action* move_from_herod(PlayerbotAI* ai) { return new SmMoveFromHerodAction(ai); }
    static Action* attack_whitemane(PlayerbotAI* ai) { return new SmAttackWhitemaneAction(ai); }
};

#endif
