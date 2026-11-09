/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AQ20TRIGGERCONTEXT_H
#define PLAYERBOTS_AQ20TRIGGERCONTEXT_H

#include "Aq20Triggers.h"
#include "NamedObjectContext.h"

class RaidAq20TriggerContext : public NamedObjectContext<Trigger>
{
public:
    RaidAq20TriggerContext()
    {
        creators["aq20 move to crystal"] = &RaidAq20TriggerContext::move_to_crystal;
        creators["aq20 buru devastate"] = &RaidAq20TriggerContext::buru_devastate;
        creators["aq20 ossirian shelter"] = &RaidAq20TriggerContext::ossirian_shelter;
    }

private:
    static Trigger* move_to_crystal(PlayerbotAI* ai) { return new Aq20MoveToCrystalTrigger(ai); }
    static Trigger* buru_devastate(PlayerbotAI* ai) { return new Aq20BuruDevastateTrigger(ai); }
    static Trigger* ossirian_shelter(PlayerbotAI* ai) { return new Aq20OssirianShelterTrigger(ai); }
};

#endif
