/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COACLASSICBAGS_H
#define _PLAYERBOT_COACLASSICBAGS_H

class PlayerbotAI;

// CoA 3.3.1: on a Classic realm (AiPlayerbot.LimitGearExpansion, MaxPlayerLevel 60 or less), the empty Portable Holes
// of a random bot give way to the Classic bag of the factory (CoaGearExpansionRules::FactoryBag), at its login and
// every five minutes after; a Portable Hole holding anything stays until it is empty.
namespace CoaClassicBags
{
void Tick(PlayerbotAI* botAI);
}

#endif
