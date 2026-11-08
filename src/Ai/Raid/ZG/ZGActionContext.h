/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_ZGACTIONCONTEXT_H
#define PLAYERBOTS_ZGACTIONCONTEXT_H

#include "NamedObjectContext.h"
#include "RaidBossScripts.h"
#include "ZGActions.h"
#include "ZGHelpers.h"

class RaidZulGurubActionContext : public NamedObjectContext<Action>
{
public:
    RaidZulGurubActionContext()
    {
        // Hakkar
        creators["hakkar mark"] = &RaidZulGurubActionContext::hakkar_mark;
        creators["hakkar avoid blood scythe"] = &RaidZulGurubActionContext::hakkar_avoid_blood_scythe;
        creators["hakkar move from blood pool"] = &RaidZulGurubActionContext::hakkar_move_from_blood_pool;
        creators["hakkar move to siphon pool"] = &RaidZulGurubActionContext::hakkar_move_to_siphon_pool;
        creators["thekal mark"] = &RaidZulGurubActionContext::thekal_mark;
        creators["jindo mark"] = &RaidZulGurubActionContext::jindo_mark;
        creators["grilek prey run"] = &RaidZulGurubActionContext::grilek_prey_run;

        // High Priest Venoxis
        creators["venoxis avoid venom spit"] = &RaidZulGurubActionContext::venoxis_avoid_venom_spit;
        creators["venoxis backup taunt"] = &RaidZulGurubActionContext::venoxis_backup_taunt;

        // Bloodlord Mandokir
        creators["bloodlord mandokir stop causing threat"] =
            &RaidZulGurubActionContext::bloodlord_mandokir_stop_causing_threat;
    }

private:
    // The Heart of Hakkar first (it only exists during Blood Siphon), then the Sons of Hakkar, then him.
    static Action* hakkar_mark(PlayerbotAI* botAI)
    {
        // CoA Combat (HK1): the heart the CoA script summons (92005) first, with the fixes on.
        std::vector<uint32> order;
        if (ZulGurubHelpers::ZgCoaFixesOn())
            order.push_back(ZulGurubHelpers::NPC_COA_HEART_OF_HAKKAR_SCRIPT);
        order.push_back(static_cast<uint32>(ZulGurubHelpers::ZulGurubNpcs::NPC_COA_HEART_OF_HAKKAR));
        order.push_back(static_cast<uint32>(ZulGurubHelpers::ZulGurubNpcs::NPC_SON_OF_HAKKAR));
        return new RaidKillOrderMarkAction(botAI, "hakkar mark", "hakkar", order);
    }
    static Action* hakkar_move_to_siphon_pool(PlayerbotAI* botAI) { return new HakkarMoveToSiphonPoolAction(botAI); }
    // CoA Combat (TK1): Lor'Khan (he heals), then Zath, then Thekal.
    static Action* thekal_mark(PlayerbotAI* botAI)
    {
        return new RaidKillOrderMarkAction(
            botAI, "thekal mark", "high priest thekal",
            { static_cast<uint32>(ZulGurubHelpers::ZulGurubNpcs::NPC_ZEALOT_LORKHAN),
              static_cast<uint32>(ZulGurubHelpers::ZulGurubNpcs::NPC_ZEALOT_ZATH) });
    }
    // CoA Combat (JD1): Brain Wash Totems, then Healing Wards, then the Shades, then Jin'do.
    static Action* jindo_mark(PlayerbotAI* botAI)
    {
        return new RaidKillOrderMarkAction(
            botAI, "jindo mark", "jin'do the hexxer",
            { ZulGurubHelpers::NPC_BRAIN_WASH_TOTEM, ZulGurubHelpers::NPC_POWERFUL_HEALING_WARD,
              ZulGurubHelpers::NPC_SHADE_OF_JINDO });
    }
    static Action* grilek_prey_run(PlayerbotAI* botAI) { return new GrilekPreyRunAction(botAI); }
    static Action* hakkar_avoid_blood_scythe(PlayerbotAI* botAI) { return new HakkarAvoidBloodScytheAction(botAI); }
    static Action* hakkar_move_from_blood_pool(PlayerbotAI* botAI)
    {
        return new RaidMoveFromGroundEffectAction(botAI, "hakkar move from blood pool");
    }
    static Action* venoxis_avoid_venom_spit(PlayerbotAI* botAI) { return new VenoxisAvoidVenomSpitAction(botAI); }
    static Action* venoxis_backup_taunt(PlayerbotAI* botAI)
    {
        return new RaidBackupTauntAction(botAI, "venoxis backup taunt", "high priest venoxis");
    }
    static Action* bloodlord_mandokir_stop_causing_threat(PlayerbotAI* botAI)
    {
        return new BloodlordMandokirStopCausingThreatAction(botAI);
    }
};

#endif
