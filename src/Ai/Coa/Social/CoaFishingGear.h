/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAFISHINGGEAR_H
#define PLAYERBOTS_COAFISHINGGEAR_H

class Item;
class Player;
class PlayerbotAI;

// CoA 3.1.6: a bot that fished takes its weapons back. A self-found bot is given no gear, so nothing else ever took
// the fishing pole out of its hands: a tank recruited from the shore kept it and held no aggro.
namespace CoaFishingGear
{
bool IsFishingPole(Item* item);
// Before the pole goes in the main hand: the weapon and the off-hand (shield) worn now are kept in mind.
void BeforePole(Player* bot);
// The pole in the main hand: the weapons kept in mind go back, else the best weapon of the bags. Nothing is created:
// with no weapon in its bags, the bot keeps the pole. True when a weapon was put in hand.
bool Restore(Player* bot);
// The safety net, on the bot's update (every 2 s at most): a pole in the main hand of a bot that is not fishing now
// (in a fight, always) gives way to a weapon (Restore): after a group, a recruit, a login or a refresh too.
void Tick(PlayerbotAI* botAI);
}  // namespace CoaFishingGear

#endif
