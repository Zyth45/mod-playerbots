/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAALTBAGS_H
#define PLAYERBOTS_COAALTBAGS_H

#include <array>
#include <cstdint>
#include <string>

class PlayerbotAI;

// "alt cmp|bags|sell|destroy" for the SquidBots addon (AiPlayerbot.CoaAltBags.Enable): compare an item with what
// the alt wears, and sell or destroy its bag items up to a quality after a preview (alt bags protocol).
class Item;
class Player;
struct ItemTemplate;

namespace CoaAltBags
{
// True when the command is one of these four (it is then answered, even when the option is off).
bool Command(PlayerbotAI* botAI, std::string const& command, std::string const& arg);

// The addon's request, put in the bot's queue: true when it is one of the four. Every request gets its reply,
// in order, on the bot's next update, in a fight or out of it (the engine's chat trigger kept one request of
// several asked at once, and the others never got their reply).
bool Enqueue(PlayerbotAI* botAI, std::string const& request);

// Puts `bag` (in the bot's bags) in bag slot `slot` (0..3), first moving the items of the bag there to free slots.
// False, and nothing changed, when they do not fit. `replaced`: the template of the bag taken off, if any.
bool PutOnBag(Player* bot, Item* bag, uint8_t slot, ItemTemplate const** replaced);
// The sizes of the four bag slots (0: empty), for PickBagSlot.
std::array<uint32_t, 4> BagSizes(Player* bot);
void Drain(PlayerbotAI* botAI);
}

#endif
