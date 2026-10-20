/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_RFCACTIONCONTEXT_H
#define PLAYERBOTS_RFCACTIONCONTEXT_H

#include "Action.h"
#include "MovementActions.h"
#include "NamedObjectContext.h"
#include "RFCActions.h"

class VanillaDungeonRFCActionContext : public NamedObjectContext<Action>
{
    public:
        VanillaDungeonRFCActionContext() {
            creators["attack oggleflint add"] = &VanillaDungeonRFCActionContext::attack_oggleflint_add;
            creators["attack jergosh add"] = &VanillaDungeonRFCActionContext::attack_jergosh_add;
            creators["attack bazzalan"] = &VanillaDungeonRFCActionContext::attack_bazzalan;
            creators["avoid fire nova"] = &VanillaDungeonRFCActionContext::avoid_fire_nova;
            creators["hide behind earth pillar"] = &VanillaDungeonRFCActionContext::hide_behind_earth_pillar;
        }
    private:
        static Action* attack_oggleflint_add(PlayerbotAI* ai) { return new AttackOggleflintAddAction(ai); }
        static Action* attack_jergosh_add(PlayerbotAI* ai) { return new AttackJergoshAddAction(ai); }
        static Action* attack_bazzalan(PlayerbotAI* ai) { return new AttackBazzalanAction(ai); }
        static Action* avoid_fire_nova(PlayerbotAI* ai) { return new TaragamanAvoidFireNovaAction(ai); }
        static Action* hide_behind_earth_pillar(PlayerbotAI* ai) { return new TaragamanHideBehindPillarAction(ai); }
};

#endif
