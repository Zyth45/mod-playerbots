/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA World: the pace of bot-only battlegrounds and arenas (CoaWorldBgPace.cpp).

#ifndef PLAYERBOTS_COAWORLDBGPACE_H
#define PLAYERBOTS_COAWORLDBGPACE_H

#include "Define.h"

class Player;

namespace CoaWorldBgPace
{
bool On();
uint32 IntervalMs();
// The bot is in a battleground or arena with no real player on the map nor in its queue (bot's map thread).
bool BotOnly(Player* bot);
}  // namespace CoaWorldBgPace

void AddSC_coa_world_bg_pace();

#endif
