/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COACITYLIFE_H
#define PLAYERBOTS_COACITYLIFE_H

#include "Common.h"
#include "ObjectGuid.h"

class Player;
class PlayerbotAI;
class WorldObject;

// CoA Live, L2 City Life (AiPlayerbot.CoaLive.City.Enable, CoaCityLifeRules.h), hooked in the "new rpg" wander
// among NPCs (NewRpgWanderNpcAction) while a random bot visits a capital. Map thread. All do nothing when off.
namespace CoaCityLife
{
// The next stop of a city visit: a station (inn, bank, auction house, anvil or forge, training dummy), or empty to
// let the bot pick any NPC as before.
ObjectGuid ChooseStation(PlayerbotAI* botAI);
// The bot reached its stop: it strikes the station's pose (sits at the inn...).
void OnReach(PlayerbotAI* botAI, WorldObject* object);
// Each update while it stays: the station's emote now and then. Returns how long the stay lasts (ms), or
// defaultMs for a plain NPC.
uint32 WhileStaying(PlayerbotAI* botAI, WorldObject* object, uint32 defaultMs);
// The stay is over: the bot stands up and frees the station.
void Leave(PlayerbotAI* botAI);
// A bot that stepped from its auctioneer into the room still stays at its station (within ROOM_KEEP yards).
bool InRoom(PlayerbotAI* botAI, WorldObject* object);
// The length of a city visit: the residents (a tenth of the random bots) stay three times as long.
uint32 VisitMs(Player* bot, uint32 cityStayMs);
}  // namespace CoaCityLife

#endif
