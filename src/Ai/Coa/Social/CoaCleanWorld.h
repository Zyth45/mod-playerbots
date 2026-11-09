/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COACLEANWORLD_H
#define PLAYERBOTS_COACLEANWORLD_H

class Player;
class PlayerbotAI;

// A clean world (AiPlayerbot.CoaLive.CleanWorld.Enable, CoaCleanWorldRules.h). Map thread (the bot's update).
namespace CoaCleanWorld
{
// Before a random bot's ghost takes its body back (ReviveFromCorpseAction): monsters on the body send the ghost to a
// safe spot within reach first, or to the spirit healer when there is none. True when it took the turn (moving,
// or the spirit healer); false: revive here, as before.
bool PrepareRevive(PlayerbotAI* botAI);
// About every 5 s (CoaLive's look): a random bot idling in the water for 30 s swims to the nearest shore.
void LeaveWater(Player* bot, PlayerbotAI* botAI);
}  // namespace CoaCleanWorld

#endif
