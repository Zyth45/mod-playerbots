/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_UKACTIONCONTEXT_H
#define PLAYERBOTS_UKACTIONCONTEXT_H

#include "Action.h"
#include "MovementActions.h"
#include "NamedObjectContext.h"
#include "TellLosAction.h"
#include "UKActions.h"

class WotlkDungeonUKActionContext : public NamedObjectContext<Action>
{
    public:
        WotlkDungeonUKActionContext() {
            creators["attack frost tomb"] = &WotlkDungeonUKActionContext::attack_frost_tomb;
            creators["attack dalronn"] = &WotlkDungeonUKActionContext::attack_dalronn;
            creators["ingvar get behind"] = &WotlkDungeonUKActionContext::ingvar_get_behind;
            creators["ingvar hide from roar"] = &WotlkDungeonUKActionContext::ingvar_hide_from_roar;
            creators["ingvar cover ready"] = &WotlkDungeonUKActionContext::ingvar_cover_ready;
            creators["ingvar hold spot"] = &WotlkDungeonUKActionContext::ingvar_hold_spot;
            creators["ingvar dodge smash"] = &WotlkDungeonUKActionContext::ingvar_dodge_smash;
            creators["ingvar smash return"] = &WotlkDungeonUKActionContext::ingvar_smash_return;
        }
    private:
        static Action* attack_frost_tomb(PlayerbotAI* ai) { return new AttackFrostTombAction(ai); }
        static Action* attack_dalronn(PlayerbotAI* ai) { return new AttackDalronnAction(ai); }
        static Action* ingvar_get_behind(PlayerbotAI* ai) { return new SetBehindTargetAction(ai); }
        static Action* ingvar_hide_from_roar(PlayerbotAI* ai) { return new IngvarHideFromRoarAction(ai); }
        static Action* ingvar_cover_ready(PlayerbotAI* ai) { return new IngvarCoverReadyAction(ai); }
        static Action* ingvar_hold_spot(PlayerbotAI* ai) { return new IngvarHoldSpotAction(ai); }
        static Action* ingvar_dodge_smash(PlayerbotAI* ai) { return new IngvarDodgeSmashAction(ai); }
        static Action* ingvar_smash_return(PlayerbotAI* ai) { return new IngvarSmashReturnAction(ai); }
};

#endif
