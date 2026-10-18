/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_MARATRIGGERCONTEXT_H
#define PLAYERBOTS_MARATRIGGERCONTEXT_H

#include "MaraTriggers.h"
#include "NamedObjectContext.h"

class VanillaDungeonMaraTriggerContext : public NamedObjectContext<Trigger>
{
public:
    VanillaDungeonMaraTriggerContext()
    {
        creators["mara hurl boulder"] = &VanillaDungeonMaraTriggerContext::hurl_boulder;
        creators["mara frontal cone"] = &VanillaDungeonMaraTriggerContext::frontal_cone;
        creators["mara dust field"] = &VanillaDungeonMaraTriggerContext::dust_field;
    }

private:
    static Trigger* hurl_boulder(PlayerbotAI* ai) { return new MaraHurlBoulderTrigger(ai); }
    static Trigger* frontal_cone(PlayerbotAI* ai) { return new MaraFrontalConeTrigger(ai); }
    static Trigger* dust_field(PlayerbotAI* ai) { return new MaraDustFieldTrigger(ai); }
};

#endif
