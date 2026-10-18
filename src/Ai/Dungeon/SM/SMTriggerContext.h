/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_SMTRIGGERCONTEXT_H
#define PLAYERBOTS_SMTRIGGERCONTEXT_H

#include "NamedObjectContext.h"
#include "SMTriggers.h"

class VanillaDungeonSMTriggerContext : public NamedObjectContext<Trigger>
{
public:
    VanillaDungeonSMTriggerContext()
    {
        creators["sm doan detonation"] = &VanillaDungeonSMTriggerContext::doan_detonation;
        creators["sm doan ranged"] = &VanillaDungeonSMTriggerContext::doan_ranged;
        creators["sm herod whirlwind"] = &VanillaDungeonSMTriggerContext::herod_whirlwind;
        creators["sm whitemane focus"] = &VanillaDungeonSMTriggerContext::whitemane_focus;
    }

private:
    static Trigger* doan_detonation(PlayerbotAI* ai) { return new SmDoanDetonationTrigger(ai); }
    static Trigger* doan_ranged(PlayerbotAI* ai) { return new SmDoanRangedTrigger(ai); }
    static Trigger* herod_whirlwind(PlayerbotAI* ai) { return new SmHerodWhirlwindTrigger(ai); }
    static Trigger* whitemane_focus(PlayerbotAI* ai) { return new SmWhitemaneFocusTrigger(ai); }
};

#endif
