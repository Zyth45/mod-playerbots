/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_BOT_PACKET_RULES_H
#define COA_BOT_PACKET_RULES_H

// Where a bot's packet is handled with AiPlayerbot.CoaWorld.BotPacketsInMapThread, apart from the game objects so that
// tests/coa_bot_packet_rules.cpp can check it. The core's own split of a real player's packets (WorldSession.cpp,
// MapSessionFilter and WorldSessionFilter): handled-in-place packets on either side, thread-unsafe ones in the world
// thread, thread-safe ones in the map thread while the player is in the world, else in the world thread.

namespace CoaBotPackets
{
    enum Place
    {
        PLACE_INPLACE = 0,     // PROCESS_INPLACE
        PLACE_THREADUNSAFE,    // PROCESS_THREADUNSAFE
        PLACE_THREADSAFE,      // PROCESS_THREADSAFE
    };

    inline bool HandleHere(Place place, bool inWorld, bool mapThread)
    {
        if (place == PLACE_INPLACE)
            return true;
        if (place == PLACE_THREADUNSAFE)
            return !mapThread;
        return mapThread ? inWorld : !inWorld;
    }
}

#endif
