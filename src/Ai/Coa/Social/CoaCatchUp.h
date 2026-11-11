#ifndef _PLAYERBOT_COACATCHUP_H
#define _PLAYERBOT_COACATCHUP_H

#include "Common.h"

#include <string>

class Player;

// AiPlayerbot.CoaFreshStart.CatchUp.* on a new living realm (CoaFreshStart.Enable): the front of the human players
// (CoaFreshStartRules.h), surveyed every 10 minutes from the database, and an hourly record of it with the random
// bots' levels for the bench (world.json "leveling").
namespace CoaCatchUp
{
void Tick(uint64 nowMs);        // world thread
uint32 Front();                 // 0: none (catch-up off, fewer active humans than MinHumans)
uint32 XpPercent(Player* bot);  // the pace and the catch-up of a random bot, in percent
std::string Json();             // the "leveling" object, empty when FreshStart is off
}  // namespace CoaCatchUp

#endif
