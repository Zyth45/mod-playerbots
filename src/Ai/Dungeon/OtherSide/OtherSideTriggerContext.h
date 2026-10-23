/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_OTHERSIDETRIGGERCONTEXT_H
#define PLAYERBOTS_OTHERSIDETRIGGERCONTEXT_H

#include "NamedObjectContext.h"
#include "OtherSideTriggers.h"

class CoaDungeonOtherSideTriggerContext : public NamedObjectContext<Trigger>
{
public:
    CoaDungeonOtherSideTriggerContext()
    {
        creators["other side in hazard"] = &CoaDungeonOtherSideTriggerContext::in_hazard;
        creators["other side egg to kick"] = &CoaDungeonOtherSideTriggerContext::egg_to_kick;
        creators["other side muzah's target"] = &CoaDungeonOtherSideTriggerContext::muzahs_target;
        creators["other side spider"] = &CoaDungeonOtherSideTriggerContext::spider;
        creators["other side egg phase"] = &CoaDungeonOtherSideTriggerContext::egg_phase;
    }
private:
    static Trigger* in_hazard(PlayerbotAI* botAI) { return new OtherSideInHazardTrigger(botAI); }
    static Trigger* egg_to_kick(PlayerbotAI* botAI) { return new OtherSideEggToKickTrigger(botAI); }
    static Trigger* muzahs_target(PlayerbotAI* botAI) { return new OtherSideMuzahsTargetTrigger(botAI); }
    static Trigger* spider(PlayerbotAI* botAI) { return new OtherSideSpiderTrigger(botAI); }
    static Trigger* egg_phase(PlayerbotAI* botAI) { return new OtherSideEggPhaseTrigger(botAI); }
};

#endif
