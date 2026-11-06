/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_MAIN_TANK_RULES_H
#define COA_MAIN_TANK_RULES_H

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

#endif
