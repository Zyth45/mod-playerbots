/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_STRATTRIGGERCONTEXT_H
#define PLAYERBOTS_STRATTRIGGERCONTEXT_H

#include "NamedObjectContext.h"
#include "StratTriggers.h"

class VanillaDungeonStratTriggerContext : public NamedObjectContext<Trigger>
{
public:
    VanillaDungeonStratTriggerContext()
    {
        creators["strat crusader's hammer"] = &VanillaDungeonStratTriggerContext::crusaders_hammer;
        creators["strat frontal cone"] = &VanillaDungeonStratTriggerContext::frontal_cone;
        creators["strat divine retribution"] = &VanillaDungeonStratTriggerContext::divine_retribution;
    }

private:
    static Trigger* crusaders_hammer(PlayerbotAI* ai) { return new StratCrusadersHammerTrigger(ai); }
    static Trigger* frontal_cone(PlayerbotAI* ai) { return new StratFrontalConeTrigger(ai); }
    static Trigger* divine_retribution(PlayerbotAI* ai) { return new StratDivineRetributionTrigger(ai); }
};

#endif
