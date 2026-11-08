/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

/*
 * CoA World: random bots borrowed by the living-world events (invasions, resource rushes...).
 *
 * One registry for all of them, so a bot is never borrowed twice and each event keeps to its own
 * budget. A borrowed bot near the spot walks there through its usual RPG travel (go grind); one
 * from farther away is brought to a gathering point, only when no real player sees it leave or
 * arrive. Released, it goes back to its usual life, and home if it was brought.
 *
 * World thread only (WorldScript::OnUpdate, commands).
 */

#ifndef COA_WORLD_BOTS_H
#define COA_WORLD_BOTS_H

#include "Define.h"
#include "Position.h"
#include "SharedDefines.h"

namespace CoaWorld
{
    enum BotOwner : uint32
    {
        BOT_OWNER_INVASION_BASE = 100,  // + invasion index
        BOT_OWNER_RUSH = 200,
    };

    struct BorrowRequest
    {
        uint32 mapId = 0;
        Position goal;            // where the bots head for
        Position rally;           // where bots from afar arrive
        uint8 team = TEAM_NEUTRAL;  // TEAM_NEUTRAL: both sides
        bool balanceTeams = false;  // half Alliance, half Horde (when team is TEAM_NEUTRAL)
        uint32 minLevel = 1;
        uint32 maxLevel = 255;
        uint32 count = 0;
        float nearRadius = 500.0f;  // closer than this on the same map: they walk
        bool teleport = true;       // farther: brought to the rally point
    };

    // Random bots in the world now.
    uint32 OnlineRandomBots();

    // pct of OnlineRandomBots(), at most max.
    uint32 BotBudget(uint32 pct, uint32 max);

    // Borrows up to request.count bots for owner. Returns how many it got.
    uint32 BorrowBots(uint32 owner, BorrowRequest const& request);

    // Hands back every bot of owner. returnHome: those brought from afar go back where they were.
    void ReleaseBots(uint32 owner, bool returnHome);

    uint32 BorrowedCount(uint32 owner);
    uint32 BorrowedTotal();
}

#endif
