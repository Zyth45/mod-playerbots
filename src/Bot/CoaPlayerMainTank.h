/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_COAPLAYERMAINTANK_H
#define _PLAYERBOT_COAPLAYERMAINTANK_H

#include "Common.h"
#include "CoaMainTankRules.h"
#include "ObjectGuid.h"

class Group;
class Player;
class Unit;

// A player who tanks a raid with bot off-tanks (SquidBots Lite "Main Tank" bar, 2.9.4). The player's flag ("tank main
// on") keeps the bot tanks off what the player holds; each bot tank can be given a target of its own ("tank target",
// marked with a raid icon), a target given back after a swap, and the rest of its orders through CoaTankAction. Nothing
// here is saved: a logout clears it. Read from the map threads, written by the bots' commands: everything is guarded,
// and the flag set has an atomic count so that a realm where nobody uses it pays one load per call.
namespace CoaPlayerMainTank
{
    // The player (not a bot) said "tank main on" and is online.
    bool IsSet(Player const* player);
    // The first member of the group on the same map as `reference` with the flag, else nullptr.
    Player* FlaggedInGroup(Group const* group, Player const* reference);
    void Set(Player* player, bool on);

    // Whether the bot tank must leave this enemy to the flagged player: its victim is that player, alive and above
    // AiPlayerbot.PlayerMainTankRescueHealth.
    bool HeldByPlayer(Player const* bot, Unit const* enemy);
    // Whether this enemy's victim is a flagged player of the bot's raid, and whether that player still holds it or
    // must be rescued (at or under AiPlayerbot.PlayerMainTankRescueHealth).
    CoaPlayerHold Hold(Player const* bot, Unit const* enemy);

    // The target given to a bot tank, and its raid icon (server index 0-7, 0xFF for none).
    void Pin(Player* bot, ObjectGuid target, uint8 icon);
    ObjectGuid Pinned(Player const* bot, uint8* icon = nullptr);
    void Unpin(Player* bot);

    // A target the bot gave back after a swap: it stays off it for a short while, so the player can taunt it back.
    void Release(Player* bot, ObjectGuid target);
    bool Released(Player const* bot, ObjectGuid target);

    // Raid icons in the order of the bar (client 6, 4, 3, 7, 1, 2: square, triangle, diamond, cross, star, circle;
    // never moon, which marks crowd control, nor skull, left to the player). Server index 0-7.
    uint8 NextFreeIcon(Group const* group);
    void Forget(ObjectGuid guid);

    // Placement ("tank hold"): the bot tank holds the point it was given, in combat too, until "tank clear" or the end
    // of its given target (07/11, RC11).
    void SetPlacement(Player* bot, bool on);
    bool InPlacement(Player const* bot);

    // The given target's end (dead, or gone: evade, out of reach), noted where the target is read and handled by the
    // bot's own update before its engine runs (TakeEnded): strategies are not changed while the engine reads them.
    void MarkEnded(Player* bot, std::string const& name, bool dead);
    bool TakeEnded(Player const* bot, std::string& name, bool& dead);
}

#endif
