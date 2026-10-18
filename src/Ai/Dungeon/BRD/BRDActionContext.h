/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_BRDACTIONCONTEXT_H
#define PLAYERBOTS_BRDACTIONCONTEXT_H

#include "Action.h"
#include "BRDActions.h"
#include "NamedObjectContext.h"

class VanillaDungeonBRDActionContext : public NamedObjectContext<Action>
{
public:
    VanillaDungeonBRDActionContext()
    {
        creators["brd move from gout of flame"] = &VanillaDungeonBRDActionContext::move_from_gout;
        creators["brd attack burning spirit"] = &VanillaDungeonBRDActionContext::attack_burning_spirit;
        creators["brd attack thaurissan"] = &VanillaDungeonBRDActionContext::attack_thaurissan;
        creators["brd move from ground tremor"] = &VanillaDungeonBRDActionContext::move_from_ground_tremor;
        creators["brd move from frontal cone"] = &VanillaDungeonBRDActionContext::move_from_frontal_cone;
    }

private:
    static Action* move_from_gout(PlayerbotAI* ai) { return new BrdMoveFromGoutAction(ai); }
    static Action* attack_burning_spirit(PlayerbotAI* ai) { return new BrdAttackBurningSpiritAction(ai); }
    static Action* attack_thaurissan(PlayerbotAI* ai) { return new BrdAttackThaurissanAction(ai); }
    static Action* move_from_ground_tremor(PlayerbotAI* ai) { return new BrdMoveFromGroundTremorAction(ai); }
    static Action* move_from_frontal_cone(PlayerbotAI* ai) { return new BrdMoveFromFrontalConeAction(ai); }
};

#endif
