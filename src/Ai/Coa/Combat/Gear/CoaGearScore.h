/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COA_GEAR_SCORE_H
#define PLAYERBOTS_COA_GEAR_SCORE_H

#include <cstdint>
#include <vector>

class Player;
struct ItemTemplate;

// The bots' gear score (CoA Combat, lot 6): item set bonuses and raid tokens count in the choice of gear.
//
// STABLE API, read by other sessions (CoA Economie: which upgrade a bot buys at the auction house). The signatures and
// the meaning below do not change without telling them; the inside may.
//
// With AiPlayerbot.CoaCombat.GearScore.Enable at 0 (the default), ItemScore is the stock score (the one the item usage
// compares with) and tokens keep their stock roll; at 1, set bonuses and tokens count.
namespace CoaGearScore
{
    // The score of `itemId` for `bot`, in the unit item usage compares: the stock stat score, plus, with the option on,
    // the set bonuses the item would unlock in `equipSlot` (EQUIPMENT_SLOT_*; -1: the slot it would go to).
    float ItemScore(Player* bot, uint32_t itemId, int32_t randomPropertyId = 0, int32_t equipSlot = -1);

    // How much better `itemId` is than what `bot` wears in the slot it would take (the weaker of two rings or two
    // trinkets), in the same unit; 0 when it is no upgrade or the bot cannot wear it. Weapons are compared slot for slot
    // (a two-hander against the main hand alone). For ranking upgrades, not a promise of equipping.
    float UpgradeGain(Player* bot, uint32_t itemId, int32_t randomPropertyId = 0);

    // The set bonuses a piece unlocks in `equipSlot` (0 when the option is off or the item is in no set).
    float SetBonusMarginal(Player* bot, ItemTemplate const* proto, int32_t equipSlot);

    // A raid token: an item a quest takes to give a piece of gear (Qiraji Bindings, Zul'Gurub's bijous and the like),
    // indexed from the quest data at first use.
    bool IsToken(uint32_t itemId);

    // The gear a token gives that `bot`'s class may take (quest class mask and item class mask).
    std::vector<uint32_t> TokenRewardsFor(Player* bot, uint32_t tokenId);
}

#endif
