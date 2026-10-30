/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_ZGTRIGGERCONTEXT_H
#define PLAYERBOTS_ZGTRIGGERCONTEXT_H

#include "NamedObjectContext.h"
#include "RaidBossScripts.h"
#include "ZGTriggers.h"

class RaidZulGurubTriggerContext : public NamedObjectContext<Trigger>
{
public:
    RaidZulGurubTriggerContext()
    {
        // Hakkar
        creators["hakkar mark"] = &RaidZulGurubTriggerContext::hakkar_mark;
        creators["hakkar blood scythe"] = &RaidZulGurubTriggerContext::hakkar_blood_scythe;
        creators["hakkar blood pool"] = &RaidZulGurubTriggerContext::hakkar_blood_pool;

        // Bloodlord Mandokir
        creators["bloodlord mandokir threatening gaze"] =
            &RaidZulGurubTriggerContext::bloodlord_mandokir_threatening_gaze;
    }

private:
    static Trigger* hakkar_mark(PlayerbotAI* botAI) { return new RaidKillOrderMarkTrigger(botAI, "hakkar mark", "hakkar"); }
    static Trigger* hakkar_blood_scythe(PlayerbotAI* botAI) { return new HakkarBloodScytheTrigger(botAI); }
    static Trigger* hakkar_blood_pool(PlayerbotAI* botAI) { return new HakkarBloodPoolTrigger(botAI); }
    static Trigger* bloodlord_mandokir_threatening_gaze(PlayerbotAI* botAI)
    {
        return new BloodlordMandokirThreateningGazeTrigger(botAI);
    }
};

#endif
