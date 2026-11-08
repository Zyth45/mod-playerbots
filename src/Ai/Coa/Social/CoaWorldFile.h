/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAWORLDFILE_H
#define PLAYERBOTS_COAWORLDFILE_H

#include "Common.h"

// world.json for the CoA Dashboard (AiPlayerbot.CoaLive.DataFile.Enable, CoaWorldFileRules.h): every 10 s, only
// while squidbot/watch.json asks for "world" (its date looked at every 5 s), built in one pass on the world thread
// and written by a thread of its own. Nothing but that look when nobody watches.
namespace CoaWorldFile
{
void Tick(uint64 nowMs);
}  // namespace CoaWorldFile

#endif
