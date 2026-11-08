/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COATOTEMRECALL_H
#define PLAYERBOTS_COATOTEMRECALL_H

class PlayerbotAI;

// Run by the bot's own update before its engine: with AiPlayerbot.CoaTotemRecall, a bot of any class puts its totems
// away a few seconds after its group's fight (CoaTotemRecallRules.h).
void CoaTotemRecallTick(PlayerbotAI* botAI);

#endif
