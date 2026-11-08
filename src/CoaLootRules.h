/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_LOOT_RULES_H
#define COA_LOOT_RULES_H

#include <cmath>
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

// Whether a loot target this far above or below the bot (dz) and this far off on the ground (dist2d) can be reached by
// walking to it. Ore on a hillside was refused at any distance once more than 3.5 m higher than the bot, so bots never
// went up the slope to it: the bound now grows with the distance (a slope up to 45 degrees) and stays 3.5 m (the
// interaction distance less 2) once at the target.
inline bool LootHeightReachable(float dz, float dist2d)
{
    float const contact = 3.5f;
    return std::fabs(dz) <= (dist2d > contact ? dist2d : contact);
}

#endif
