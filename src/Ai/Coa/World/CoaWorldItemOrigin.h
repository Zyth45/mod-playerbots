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

// While one lives, the pieces this thread equips with no origin noted are "start" gear: the factory's and the
// auto-gear's only. Outside it, a piece of unseen origin gets no row.
class StartGearScope
{
public:
    StartGearScope();
    ~StartGearScope();
    StartGearScope(StartGearScope const&) = delete;
    StartGearScope& operator=(StartGearScope const&) = delete;
};
}

#endif
