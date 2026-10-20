/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_RFCTRIGGERCONTEXT_H
#define PLAYERBOTS_RFCTRIGGERCONTEXT_H

#include "NamedObjectContext.h"
#include "RFCTriggers.h"

class VanillaDungeonRFCTriggerContext : public NamedObjectContext<Trigger>
{
    public:
        VanillaDungeonRFCTriggerContext()
        {
            creators["oggleflint adds"] = &VanillaDungeonRFCTriggerContext::oggleflint_adds;
            creators["jergosh adds"] = &VanillaDungeonRFCTriggerContext::jergosh_adds;
            creators["bazzalan priority"] = &VanillaDungeonRFCTriggerContext::bazzalan_priority;
            creators["taragaman fire nova"] = &VanillaDungeonRFCTriggerContext::taragaman_fire_nova;
            creators["taragaman massive fire nova"] = &VanillaDungeonRFCTriggerContext::taragaman_massive_fire_nova;
        }
    private:
        static Trigger* oggleflint_adds(PlayerbotAI* ai) { return new OggleflintAddsTrigger(ai); }
        static Trigger* jergosh_adds(PlayerbotAI* ai) { return new JergoshAddsTrigger(ai); }
        static Trigger* bazzalan_priority(PlayerbotAI* ai) { return new BazzalanDpsTrigger(ai); }
        static Trigger* taragaman_fire_nova(PlayerbotAI* ai) { return new TaragamanFireNovaTrigger(ai); }
        static Trigger* taragaman_massive_fire_nova(PlayerbotAI* ai)
        {
            return new TaragamanMassiveFireNovaTrigger(ai);
        }
};

#endif
