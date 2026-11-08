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
#include "ObjectGuid.h"
#include "SharedDefines.h"

#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>

class Player;

namespace CoaWorld
{
    enum BotOwner : uint32
    {
        BOT_OWNER_INVASION_BASE = 100,  // + invasion index
        BOT_OWNER_RUSH = 200,
        BOT_OWNER_PVP_SKIRMISH = 300,
        BOT_OWNER_PVP_DEFENCE_BASE = 400,   // + capital index
        BOT_OWNER_TRAVEL_BASE = 500,        // + route * 2 + dock
        BOT_OWNER_DUNGEON_SIM_BASE = 1000,  // + run id
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
        bool (*filter)(Player*) = nullptr;  // extra condition on the bots (BorrowBots only)
    };

    // Random bots in the world now.
    uint32 OnlineRandomBots();

    // pct of OnlineRandomBots(), at most max.
    uint32 BotBudget(uint32 pct, uint32 max);

    // Borrows up to request.count bots for owner. Returns how many it got.
    uint32 BorrowBots(uint32 owner, BorrowRequest const& request);

    // Hands back every bot of owner. returnHome: those brought from afar go back where they were.
    void ReleaseBots(uint32 owner, bool returnHome);

    // A character under a PvP ruleset of CoA (High Risk, or War Mode without the PvE marker): the only
    // ones a bot starts an open-world fight with (EnemyPlayerValue, AiPlayerbot.CoaRulesetForBots).
    bool HasPvpRuleset(Player* player);

    // Free for an event: online random bot, alive, out of combat, group, battleground and instance,
    // without a real-player master and not borrowed already.
    bool IsFreeBot(Player* bot);

    // Sends that bot (free, checked by the caller) as BorrowBots would. False if it could not go.
    bool SendBot(uint32 owner, Player* bot, BorrowRequest const& request);

    // Drops the bot from owner's list without touching it (it logs out, for example).
    void ForgetBot(uint32 owner, ObjectGuid guid);

    // The bots owner has now (online or not).
    std::vector<ObjectGuid> BorrowedBots(uint32 owner);

    // Offline holds: a bot logged out by an event (inside a dungeon, aboard a zeppelin) is not logged
    // back in by RandomPlayerbotMgr before HoldBot's time, or ReleaseHold. Memory only: a restart
    // clears them, and each ends by itself at its time.
    void HoldBot(uint32 lowGuid, time_t until);
    void ReleaseHold(uint32 lowGuid);
    bool IsHeld(uint32 lowGuid);

    // Lines of IA MIND's bank (playerbots_coa_live_*, through CoaLive), with our English text as fallback when
    // CoaLive is off or the bank has nothing for the situation.
    using Slots = std::unordered_map<std::string, std::string>;
    std::string WorldLine(Player* reader, char const* situation, Slots const& slots, std::string const& fallback);
    bool BotLine(Player* bot, char const* situation, Slots const& slots);
    // A few of owner's bots borrowed since index "from" say a line of the situation (at most "most" of them).
    void BorrowedSay(uint32 owner, size_t from, char const* situation, Slots const& slots, uint32 most);
    // The invader of an event in the reader's language (IA MIND's %event, never at the start of a line).
    std::string EventName(Player* reader, std::string const& key);

    uint32 BorrowedCount(uint32 owner);
    uint32 BorrowedTotal();
}

// For RandomPlayerbotMgr::ProcessBot: a bot held offline by a CoA World event (see HoldBot).
bool CoaWorldIsBotHeld(uint32 lowGuid);

// For RandomPlayerbotMgr::RandomTeleport: a bot moving to a new home in sight of a real player casts its
// hearthstone (the real 10 s spell, to the home just set) instead of not moving. True if it does.
// With AiPlayerbot.CoaWorld.Travel.Enable or .Hearthstone off: a plain bool, false.
bool CoaWorldHearthInstead(Player* bot);

#endif
