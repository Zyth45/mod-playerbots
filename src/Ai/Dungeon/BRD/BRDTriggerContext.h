/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_BRDTRIGGERCONTEXT_H
#define PLAYERBOTS_BRDTRIGGERCONTEXT_H

#include "BRDTriggers.h"
#include "NamedObjectContext.h"

class VanillaDungeonBRDTriggerContext : public NamedObjectContext<Trigger>
{
public:
    VanillaDungeonBRDTriggerContext()
    {
        creators["brd ironhand gout"] = &VanillaDungeonBRDTriggerContext::ironhand_gout;
        creators["brd burning spirit"] = &VanillaDungeonBRDTriggerContext::burning_spirit;
        creators["brd thaurissan focus"] = &VanillaDungeonBRDTriggerContext::thaurissan_focus;
        creators["brd ground tremor"] = &VanillaDungeonBRDTriggerContext::ground_tremor;
        creators["brd frontal cone"] = &VanillaDungeonBRDTriggerContext::frontal_cone;
    }

private:
    static Trigger* ironhand_gout(PlayerbotAI* ai) { return new BrdIronhandGoutTrigger(ai); }
    static Trigger* burning_spirit(PlayerbotAI* ai) { return new BrdBurningSpiritTrigger(ai); }
    static Trigger* thaurissan_focus(PlayerbotAI* ai) { return new BrdThaurissanFocusTrigger(ai); }
    static Trigger* ground_tremor(PlayerbotAI* ai) { return new BrdGroundTremorTrigger(ai); }
    static Trigger* frontal_cone(PlayerbotAI* ai) { return new BrdFrontalConeTrigger(ai); }
};

#endif
