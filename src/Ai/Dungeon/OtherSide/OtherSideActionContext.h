/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_OTHERSIDEACTIONCONTEXT_H
#define PLAYERBOTS_OTHERSIDEACTIONCONTEXT_H

#include "NamedObjectContext.h"
#include "OtherSideActions.h"

class CoaDungeonOtherSideActionContext : public NamedObjectContext<Action>
{
public:
    CoaDungeonOtherSideActionContext()
    {
        creators["other side leave hazard"] = &CoaDungeonOtherSideActionContext::leave_hazard;
        creators["other side kick egg"] = &CoaDungeonOtherSideActionContext::kick_egg;
        creators["other side close in on muzah"] = &CoaDungeonOtherSideActionContext::close_in_on_muzah;
        creators["other side attack spider"] = &CoaDungeonOtherSideActionContext::attack_spider;
        creators["other side run around"] = &CoaDungeonOtherSideActionContext::run_around;
    }
private:
    static Action* leave_hazard(PlayerbotAI* botAI) { return new OtherSideLeaveHazardAction(botAI); }
    static Action* kick_egg(PlayerbotAI* botAI) { return new OtherSideKickEggAction(botAI); }
    static Action* close_in_on_muzah(PlayerbotAI* botAI) { return new OtherSideCloseInOnMuzahAction(botAI); }
    static Action* attack_spider(PlayerbotAI* botAI) { return new OtherSideAttackSpiderAction(botAI); }
    static Action* run_around(PlayerbotAI* botAI) { return new OtherSideRunAroundAction(botAI); }
};

#endif
