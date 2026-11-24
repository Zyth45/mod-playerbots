/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAWORLDITEMORIGIN_H
#define PLAYERBOTS_COAWORLDITEMORIGIN_H

#include "Define.h"

class Item;

namespace CoaWorld
{
// How an item came: written to characters.coa_item_origin when it is worn (CoaWorldItemOrigin.cpp). source is a
// string literal ("dungeonsim", ...), detail its entry (a creature, a quest, a vendor, a map) or 0.
void NoteItemOrigin(Item* item, char const* source, uint32 detail);
}

#endif
