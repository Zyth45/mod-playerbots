/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_LOOT_RULES_H
#define COA_LOOT_RULES_H

#include <cstdint>

// Whether the loot a game object holds now is what the bot would get by opening it, apart from the game objects, so
// that tests/coa_loot_rules.cpp can check it. A chest ready to be opened (loot state GO_READY) with a loot table is
// cleared and rolled again for its next opener (Player::SendLoot): what it still holds is the previous opener's loot.
// A worldforged pickup the player had just emptied held the player's spent item, so the alt following that player
// judged it had nothing to take and never opened its own (random bots seldom come right after someone else).
inline bool CoaHeldLootIsWhatOpenerGets(bool lootStateReady, std::uint32_t lootId)
{
    return !(lootStateReady && lootId);
}

#endif
