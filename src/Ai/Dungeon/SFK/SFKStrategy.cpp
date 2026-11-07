/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "SFKStrategy.h"
#include "SFKMultipliers.h"

void VanillaDungeonSFKStrategy::InitTriggers(std::vector<TriggerNode*> &triggers)
{
    // Rethilgore, Commander Springvale, Odo the Blindwatcher, Fenrus the Devourer
    // Their dangerous spells are all interruptible casts or dispellable auras: Soul Drain, Holy
    // Light, Howling Rage (cast once per health threshold, so an interrupted one never returns),
    // Toxic Saliva and Silverlaine's Veil of Shadow. The class interrupt and dispel actions
    // already take them; nothing here would add to that.

    // Wolf Master Nandos
    // His pack and his summons live for four minutes and do not leave with him, so they go first.
    // A Lupine Delusion hitting one of us goes right after the Horrors: it vanishes on the first hit it takes.
    triggers.push_back(new TriggerNode("nandos pack",
            { NextAction("attack nandos pack", ACTION_RAID + 1) }));

    // Archmage Arugal
    // Thundershock stuns everyone within five yards of him. Only the ranged are moved; melee have
    // to be there.
    //
    // Arugal's Curse is not handled. The charmed player is hostile to the group, so no friendly
    // dispel reaches them, and the bots already refuse a charmed unit as their target; the ten
    // seconds simply run out.
    triggers.push_back(new TriggerNode("arugal thundershock",
            { NextAction("avoid thundershock", ACTION_MOVE + 5) }));
}

void VanillaDungeonSFKStrategy::InitMultipliers(std::vector<Multiplier*> &multipliers)
{
    multipliers.push_back(new WolfMasterNandosMultiplier(botAI));
    multipliers.push_back(new ArchmageArugalMultiplier(botAI));
}
