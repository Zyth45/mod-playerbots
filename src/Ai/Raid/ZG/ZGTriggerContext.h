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
        creators["hakkar siphon pool"] = &RaidZulGurubTriggerContext::hakkar_siphon_pool;
        creators["thekal mark"] = &RaidZulGurubTriggerContext::thekal_mark;
        creators["jindo mark"] = &RaidZulGurubTriggerContext::jindo_mark;
        creators["grilek pursuit"] = &RaidZulGurubTriggerContext::grilek_pursuit;

        // High Priest Venoxis
        creators["venoxis venom spit"] = &RaidZulGurubTriggerContext::venoxis_venom_spit;
        creators["venoxis backup taunt"] = &RaidZulGurubTriggerContext::venoxis_backup_taunt;

        // Bloodlord Mandokir
        creators["bloodlord mandokir threatening gaze"] =
            &RaidZulGurubTriggerContext::bloodlord_mandokir_threatening_gaze;
    }

private:
    static Trigger* hakkar_mark(PlayerbotAI* botAI) { return new RaidKillOrderMarkTrigger(botAI, "hakkar mark", "hakkar"); }
    static Trigger* hakkar_blood_scythe(PlayerbotAI* botAI) { return new HakkarBloodScytheTrigger(botAI); }
    static Trigger* hakkar_blood_pool(PlayerbotAI* botAI) { return new HakkarBloodPoolTrigger(botAI); }
    static Trigger* hakkar_siphon_pool(PlayerbotAI* botAI) { return new HakkarSiphonPoolTrigger(botAI); }
    static Trigger* thekal_mark(PlayerbotAI* botAI)
    {
        return new ZulGurubCoaKillOrderTrigger(botAI, "thekal mark", "high priest thekal");
    }
    static Trigger* jindo_mark(PlayerbotAI* botAI)
    {
        return new ZulGurubCoaKillOrderTrigger(botAI, "jindo mark", "jin'do the hexxer");
    }
    static Trigger* grilek_pursuit(PlayerbotAI* botAI) { return new GrilekPursuitTrigger(botAI); }
    static Trigger* venoxis_venom_spit(PlayerbotAI* botAI) { return new VenoxisVenomSpitTrigger(botAI); }
    static Trigger* venoxis_backup_taunt(PlayerbotAI* botAI)
    {
        return new RaidBackupTauntTrigger(botAI, "venoxis backup taunt", "high priest venoxis");
    }
    static Trigger* bloodlord_mandokir_threatening_gaze(PlayerbotAI* botAI)
    {
        return new BloodlordMandokirThreateningGazeTrigger(botAI);
    }
};

#endif
