/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_RFDTRIGGERCONTEXT_H
#define PLAYERBOTS_RFDTRIGGERCONTEXT_H

#include "NamedObjectContext.h"
#include "RFDTriggers.h"

class VanillaDungeonRFDTriggerContext : public NamedObjectContext<Trigger>
{
public:
    VanillaDungeonRFDTriggerContext()
    {
        creators["rfd glutton expel bloat"] = &VanillaDungeonRFDTriggerContext::glutton_expel_bloat;
        creators["rfd mordresh bone storm"] = &VanillaDungeonRFDTriggerContext::mordresh_bone_storm;
        creators["rfd amnennar mark"] = &VanillaDungeonRFDTriggerContext::amnennar_mark;
    }

private:
    static Trigger* glutton_expel_bloat(PlayerbotAI* ai) { return new RfdGluttonExpelBloatTrigger(ai); }
    static Trigger* mordresh_bone_storm(PlayerbotAI* ai) { return new RfdMordreshBoneStormTrigger(ai); }
    static Trigger* amnennar_mark(PlayerbotAI* ai) { return new RfdAmnennarMarkTrigger(ai); }
};

#endif
