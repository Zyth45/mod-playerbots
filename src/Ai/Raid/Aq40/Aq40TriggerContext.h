/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AQ40TRIGGERCONTEXT_H
#define PLAYERBOTS_AQ40TRIGGERCONTEXT_H

#include "Aq40Triggers.h"
#include "NamedObjectContext.h"

class RaidAq40TriggerContext : public NamedObjectContext<Trigger>
{
public:
    RaidAq40TriggerContext()
    {
        creators["aq40 cthun leave stomach"] = &RaidAq40TriggerContext::leave_stomach;
        creators["aq40 cthun dark glare"] = &RaidAq40TriggerContext::dark_glare;
        creators["aq40 cthun eldritch revelations"] = &RaidAq40TriggerContext::eldritch;
        creators["aq40 cthun devour"] = &RaidAq40TriggerContext::devour;
        creators["aq40 cthun center"] = &RaidAq40TriggerContext::center;
        creators["aq40 cthun position"] = &RaidAq40TriggerContext::position;
        creators["aq40 cthun target"] = &RaidAq40TriggerContext::target;
        creators["aq40 skeram target"] = &RaidAq40TriggerContext::skeram_target;
        creators["aq40 skeram hold"] = &RaidAq40TriggerContext::skeram_hold;
        creators["aq40 skeram psychic shock"] = &RaidAq40TriggerContext::skeram_psychic_shock;
        creators["aq40 skeram gaze spread"] = &RaidAq40TriggerContext::skeram_gaze_spread;
    }

private:
    static Trigger* leave_stomach(PlayerbotAI* ai) { return new Aq40CthunLeaveStomachTrigger(ai); }
    static Trigger* dark_glare(PlayerbotAI* ai) { return new Aq40CthunDarkGlareTrigger(ai); }
    static Trigger* eldritch(PlayerbotAI* ai) { return new Aq40CthunEldritchTrigger(ai); }
    static Trigger* devour(PlayerbotAI* ai) { return new Aq40CthunDevourTrigger(ai); }
    static Trigger* center(PlayerbotAI* ai) { return new Aq40CthunCenterTrigger(ai); }
    static Trigger* position(PlayerbotAI* ai) { return new Aq40CthunPositionTrigger(ai); }
    static Trigger* target(PlayerbotAI* ai) { return new Aq40CthunTargetTrigger(ai); }
    static Trigger* skeram_target(PlayerbotAI* ai) { return new Aq40SkeramTargetTrigger(ai); }
    static Trigger* skeram_hold(PlayerbotAI* ai) { return new Aq40SkeramHoldTrigger(ai); }
    static Trigger* skeram_psychic_shock(PlayerbotAI* ai) { return new Aq40SkeramPsychicShockTrigger(ai); }
    static Trigger* skeram_gaze_spread(PlayerbotAI* ai) { return new Aq40SkeramGazeSpreadTrigger(ai); }
};

#endif
