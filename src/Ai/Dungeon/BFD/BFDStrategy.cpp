/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "BFDStrategy.h"

#include "BFDMultipliers.h"

void VanillaDungeonBFDStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Lorgus Jett, Twilight Lord Kelris, Aku'mai, Old Serra'kis: their CoA kits are Fierce Blow on the tank,
    // interruptible casts (Lightning Bolt, Mind Blast, Sleep), a purgeable Lightning Shield, and area hits that
    // reach the whole room (Poison Bolt Storm, 100 yd). The class interrupt, purge and dispel actions cover them.
    // Ghamoo-ra's Thunder Clap and Trample and Lady Sarevess's Frost Nova reach 10 yd around the boss: the
    // "coa boss dodge" strategy keeps casters out of instant point blank spells already.

    // Ghamoo-ra: while Torrent is cast and while it turns once around him, everyone, the tank included, keeps to
    // his back. He does not attack while it turns. Level with "avoid aoe": a bot caught is knocked back again
    // every 0.25 s.
    triggers.push_back(new TriggerNode("bfd ghamoo-ra torrent",
            { NextAction("bfd move from torrent", ACTION_EMERGENCY) }));

    // Lady Sarevess (Forked Lightning) and Gelihast (Cone of Cold) cast a cone at the tank: the others stand
    // to the side or behind. "coa boss dodge" only acts on cones of up to 15 yd while they are cast, which
    // misses the 30 yd Forked Lightning and the instant Cone of Cold.
    triggers.push_back(new TriggerNode("bfd frontal cone",
            { NextAction("bfd move from frontal cone", ACTION_RAID + 1) }));
}

void VanillaDungeonBFDStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new GhamooraMultiplier(botAI));
}
