/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_BRSTRIGGERCONTEXT_H
#define PLAYERBOTS_BRSTRIGGERCONTEXT_H

#include "BRSTriggers.h"
#include "NamedObjectContext.h"

class VanillaDungeonBRSTriggerContext : public NamedObjectContext<Trigger>
{
public:
    VanillaDungeonBRSTriggerContext()
    {
        creators["brs line attack"] = &VanillaDungeonBRSTriggerContext::line_attack;
        creators["brs rend bladestorm"] = &VanillaDungeonBRSTriggerContext::rend_bladestorm;
        creators["brs frontal cone"] = &VanillaDungeonBRSTriggerContext::frontal_cone;
    }

private:
    static Trigger* line_attack(PlayerbotAI* ai) { return new BrsLineAttackTrigger(ai); }
    static Trigger* rend_bladestorm(PlayerbotAI* ai) { return new BrsRendBladestormTrigger(ai); }
    static Trigger* frontal_cone(PlayerbotAI* ai) { return new BrsFrontalConeTrigger(ai); }
};

#endif
