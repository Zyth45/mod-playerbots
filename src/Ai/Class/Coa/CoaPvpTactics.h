/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

// CoA bots in PvP (arenas, duels, battlegrounds): what the generic arena tactics ask of CoaPvpTactics.cpp. The
// rest of it (its strategy "coa pvp", actions and triggers) is registered by CoaAiObjectContext.

#ifndef PLAYERBOTS_COAPVPTACTICS_H
#define PLAYERBOTS_COAPVPTACTICS_H

#include "Define.h"

class Player;
class Unit;
struct Position;

namespace CoaPvp
{
// The arena spot a CoA bot takes before the fight, in the values ArenaTactics::moveToCenter reads: 3 at the back
// (healers), 6 in front (melee), 9 between them (ranged). 0 when it is not a CoA bot or
// AiPlayerbot.CoaArenaRoleSpot is off.
uint32 ArenaSpot(Player* bot);

// Whether this bot looks for a spot in sight of its target itself (AiPlayerbot.CoaArenaLineOfSight), rather than
// walking onto the target's own spot.
bool HandlesLineOfSight(Player* bot);

// A reachable point near the bot, within `range` of `target` and in its sight. False when none is found.
bool FindSightSpot(Player* bot, Unit* target, float range, Position& spot);
}

#endif
