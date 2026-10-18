/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "SMStrategy.h"

#include "SMMultipliers.h"

void VanillaDungeonSMStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Graveyard (Interrogator Vishas, Bloodmage Thalnos, Azshir, Ironspine, Fallen Champion), Library (Houndmaster
    // Loksey) and Armory: their CoA kits are Fierce Blow on the tank, interruptible casts (Shadow Bolt, Flame Spike,
    // Heal), dispellable debuffs and fears. The class interrupt, dispel and avoid-aoe actions already cover them.

    // Arcanist Doan (Library): everyone leaves Detonation while Arcane Bubble holds; the rest of the fight casters
    // and healers stand outside his Silence and Arcanist's Explosion.
    triggers.push_back(new TriggerNode("sm doan detonation",
            { NextAction("sm move from doan", ACTION_RAID + 1) }));
    triggers.push_back(new TriggerNode("sm doan ranged",
            { NextAction("sm doan ranged keep away", ACTION_RAID) }));

    // Herod (Armory): the damage dealers and healers step out of Whirlwind; the tank holds him.
    triggers.push_back(new TriggerNode("sm herod whirlwind",
            { NextAction("sm move from herod", ACTION_RAID + 1) }));

    // Mograine and Whitemane (Cathedral): once she has raised him, Whitemane dies first. She heals, shields and is
    // the one without plate.
    triggers.push_back(new TriggerNode("sm whitemane focus",
            { NextAction("sm attack whitemane", ACTION_RAID) }));
}

void VanillaDungeonSMStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new ArcanistDoanMultiplier(botAI));
    multipliers.push_back(new HerodMultiplier(botAI));
    multipliers.push_back(new WhitemaneMultiplier(botAI));
}
