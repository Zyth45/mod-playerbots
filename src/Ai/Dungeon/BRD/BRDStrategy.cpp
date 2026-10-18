/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "BRDStrategy.h"

#include "BRDMultipliers.h"

void VanillaDungeonBRDStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // The other bosses (Gerstahn, the Ring of Law, Bael'Gar, Houndmaster Grebmar, Pyromancer Loregrain, Angerforge,
    // Golem Lord Argelmach, the Grim Guzzler, Lord Incendius, Warder Stilgiss, Verek, Fineous Darkvire, the Seven):
    // their CoA kits are Fierce Blow on the tank, interruptible casts (Fireball Volley, Chain Lightning, Prayer of
    // Healing, Heal, Banish, Mana Burn), adds and totems the dungeon priorities kill first (Anvilrage Medics,
    // Scorching Totem, Spawn of Bael'Gar), ground effects (Fire Storm, Blizzard) and point blank spells of up to
    // 10 yd. The class actions, "avoid aoe" and "coa boss dodge" cover them; Phalanx's Fireball Volley and
    // Hurley's Flame Breath too.

    // Magmus: everyone, the tank too, steps out of the Ironhand Guardians' Gout of Flame.
    triggers.push_back(new TriggerNode("brd ironhand gout",
            { NextAction("brd move from gout of flame", ACTION_RAID + 2) }));

    // Ambassador Flamelash: the ranged damage dealers kill the Burning Spirits before they reach him.
    triggers.push_back(new TriggerNode("brd burning spirit",
            { NextAction("brd attack burning spirit", ACTION_RAID + 1) }));

    // Emperor Dagran Thaurissan: while Princess Moira heals him, the damage dealers stay on him; once he dies,
    // she stops fighting.
    triggers.push_back(new TriggerNode("brd thaurissan focus",
            { NextAction("brd attack thaurissan", ACTION_RAID) }));

    // Lord Roccor, Grizzle: casters and healers stand outside Ground Tremor.
    triggers.push_back(new TriggerNode("brd ground tremor",
            { NextAction("brd move from ground tremor", ACTION_RAID) }));

    // Seeth'rel: nobody but the tank stands in front of Cone of Cold, which has no cast bar to react to.
    triggers.push_back(new TriggerNode("brd frontal cone",
            { NextAction("brd move from frontal cone", ACTION_RAID + 1) }));
}

void VanillaDungeonBRDStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new AmbassadorFlamelashMultiplier(botAI));
    multipliers.push_back(new ThaurissanMultiplier(botAI));
}
