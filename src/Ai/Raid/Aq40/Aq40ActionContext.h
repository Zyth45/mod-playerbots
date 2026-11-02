/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AQ40ACTIONCONTEXT_H
#define PLAYERBOTS_AQ40ACTIONCONTEXT_H

#include "Action.h"
#include "Aq40Actions.h"
#include "NamedObjectContext.h"

class RaidAq40ActionContext : public NamedObjectContext<Action>
{
public:
    RaidAq40ActionContext()
    {
        creators["aq40 cthun leave stomach"] = &RaidAq40ActionContext::leave_stomach;
        creators["aq40 cthun dodge glare"] = &RaidAq40ActionContext::dodge_glare;
        creators["aq40 cthun look away"] = &RaidAq40ActionContext::look_away;
        creators["aq40 cthun avoid devour"] = &RaidAq40ActionContext::avoid_devour;
        creators["aq40 cthun spit out"] = &RaidAq40ActionContext::spit_out;
        creators["aq40 cthun move to spot"] = &RaidAq40ActionContext::move_to_spot;
        creators["aq40 cthun attack"] = &RaidAq40ActionContext::attack;
        creators["aq40 skeram attack"] = &RaidAq40ActionContext::skeram_attack;
        creators["aq40 skeram stop attack"] = &RaidAq40ActionContext::skeram_stop_attack;
        creators["aq40 skeram avoid psychic shock"] = &RaidAq40ActionContext::skeram_avoid_psychic_shock;
        creators["aq40 skeram gaze spread"] = &RaidAq40ActionContext::skeram_gaze_spread;
    }

private:
    static Action* leave_stomach(PlayerbotAI* ai) { return new Aq40CthunLeaveStomachAction(ai); }
    static Action* dodge_glare(PlayerbotAI* ai) { return new Aq40CthunDodgeGlareAction(ai); }
    static Action* look_away(PlayerbotAI* ai) { return new Aq40CthunLookAwayAction(ai); }
    static Action* avoid_devour(PlayerbotAI* ai) { return new Aq40CthunAvoidDevourAction(ai); }
    static Action* spit_out(PlayerbotAI* ai) { return new Aq40CthunSpitOutAction(ai); }
    static Action* move_to_spot(PlayerbotAI* ai) { return new Aq40CthunMoveToSpotAction(ai); }
    static Action* attack(PlayerbotAI* ai) { return new Aq40CthunAttackAction(ai); }
    static Action* skeram_attack(PlayerbotAI* ai) { return new Aq40SkeramAttackAction(ai); }
    static Action* skeram_stop_attack(PlayerbotAI* ai) { return new Aq40SkeramStopAttackAction(ai); }
    static Action* skeram_avoid_psychic_shock(PlayerbotAI* ai) { return new Aq40SkeramAvoidPsychicShockAction(ai); }
    static Action* skeram_gaze_spread(PlayerbotAI* ai) { return new Aq40SkeramGazeSpreadAction(ai); }
};

#endif
