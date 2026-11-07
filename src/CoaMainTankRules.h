/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_MAIN_TANK_RULES_H
#define COA_MAIN_TANK_RULES_H

#include <vector>

// What a bot tank does with an enemy when a player tanks for the group ("tank main on", SquidBots Lite Main Tank bar),
// apart from the game objects, so that tests/coa_main_tank_rules.cpp can check it. The flag is the player's word: what
// the player holds is held, whatever the player's spec and whichever bot carries the group's main tank flag; under
// AiPlayerbot.PlayerMainTankRescueHealth the player holds nothing any more and the bots take it.
enum class CoaPlayerHold
{
    None,   // the enemy's victim is not a flagged player of the raid
    Holds,  // it is, alive and above the rescue health
    Rescue  // it is, at or under the rescue health
};

// Whether the bot "has aggro" on an enemy: on itself, or held by another tank (only the main tank set on the group
// takes from another tank).
inline bool CoaHasAggro(bool onBot, CoaPlayerHold hold, bool victimIsTank, bool botIsExplicitMainTank)
{
    if (onBot)
        return true;
    if (hold != CoaPlayerHold::None)
        return hold == CoaPlayerHold::Holds;
    return victimIsTank && !botIsExplicitMainTank;
}

// Whether another tank holds the enemy, so the bot must not taunt it.
inline bool CoaHeldByOtherTank(bool onOtherPlayer, CoaPlayerHold hold, bool victimIsTank, bool botIsExplicitMainTank)
{
    if (hold != CoaPlayerHold::None)
        return hold == CoaPlayerHold::Holds;
    return onOtherPlayer && victimIsTank && !botIsExplicitMainTank;
}

// Whether an enemy on a group member is loose, for the tank to pick up.
inline bool CoaLooseOnMember(CoaPlayerHold hold, bool victimIsTank)
{
    if (hold != CoaPlayerHold::None)
        return hold == CoaPlayerHold::Rescue;
    return !victimIsTank;
}

// The member who carries the group's main tank flag once roles change (".playerbots coa raid" sets it on a tank; a
// bot that turns damage dealer by "tank spec dps", a regear or "talents spec" must not keep it). Seats in member
// order; -1: nobody. A player's flag is the player's word and stays. A bot's flag stays while the bot is a tank;
// otherwise it goes to the first living tank, else the first tank, else nobody. With assignIfNone, a group without the
// flag gets one the same way (the raid builder); without it, only a flag that went stale moves.
struct CoaMainTankSeat
{
    bool bot;
    bool tank;
    bool alive;
    bool flagged;
};

inline int CoaMainTankFlagHolder(std::vector<CoaMainTankSeat> const& seats, bool assignIfNone)
{
    int flagged = -1;
    for (size_t i = 0; i < seats.size() && flagged < 0; ++i)
        if (seats[i].flagged)
            flagged = int(i);
    if (flagged >= 0 && (!seats[flagged].bot || seats[flagged].tank))
        return flagged;
    if (flagged < 0 && !assignIfNone)
        return -1;
    for (size_t i = 0; i < seats.size(); ++i)
        if (seats[i].tank && seats[i].alive)
            return int(i);
    for (size_t i = 0; i < seats.size(); ++i)
        if (seats[i].tank)
            return int(i);
    return -1;
}

// How a bot tank's given target ends (TankTargetValue::PinnedOrAssist): still its order, dead, or gone (despawned, out
// of the bot's map, evading after a wipe or a leash, no longer attackable). Dead or gone ends the order like
// "tank clear": icon off, placement lifted, back to assisting the player (07/11, RC11).
enum class CoaPinEnd
{
    Keep,
    Dead,
    Gone
};

inline CoaPinEnd CoaPinnedTargetEnd(bool found, bool alive, bool sameMap, bool evading, bool attackable)
{
    if (found && !alive)
        return CoaPinEnd::Dead;
    if (!found || !sameMap || evading || !attackable)
        return CoaPinEnd::Gone;
    return CoaPinEnd::Keep;
}

// Placement in combat: a bot tank away from its point walks there first (no swing, reach or cast on the way, only a
// taunt it can throw while walking); at the point, it stays and the enemy comes to it.
constexpr float CoaPlacementTolerance = 3.0f;

inline bool CoaPlacementMustWalk(bool placement, bool inCombat, float distanceToPoint)
{
    return placement && inCombat && distanceToPoint > CoaPlacementTolerance;
}

// Whether to taunt the given target on the way or at the point: it hits someone else, and a taunt is ready and reaches.
inline bool CoaPlacementTaunt(bool targetOnBot, bool tauntReady, bool inRange)
{
    return !targetOnBot && tauntReady && inRange;
}
#endif
