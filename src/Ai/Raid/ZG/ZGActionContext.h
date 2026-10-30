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

        // Bloodlord Mandokir
        creators["bloodlord mandokir stop causing threat"] =
            &RaidZulGurubActionContext::bloodlord_mandokir_stop_causing_threat;
    }

private:
    // The Heart of Hakkar first (it only exists during Blood Siphon), then the Sons of Hakkar, then him.
    static Action* hakkar_mark(PlayerbotAI* botAI)
    {
        return new RaidKillOrderMarkAction(
            botAI, "hakkar mark", "hakkar",
            { static_cast<uint32>(ZulGurubHelpers::ZulGurubNpcs::NPC_COA_HEART_OF_HAKKAR),
              static_cast<uint32>(ZulGurubHelpers::ZulGurubNpcs::NPC_SON_OF_HAKKAR) });
    }
    static Action* hakkar_avoid_blood_scythe(PlayerbotAI* botAI) { return new HakkarAvoidBloodScytheAction(botAI); }
    static Action* hakkar_move_from_blood_pool(PlayerbotAI* botAI)
    {
        return new RaidMoveFromGroundEffectAction(botAI, "hakkar move from blood pool");
    }
    static Action* bloodlord_mandokir_stop_causing_threat(PlayerbotAI* botAI)
    {
        return new BloodlordMandokirStopCausingThreatAction(botAI);
    }
};

#endif
