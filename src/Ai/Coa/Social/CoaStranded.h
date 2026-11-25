/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COASTRANDED_H
#define PLAYERBOTS_COASTRANDED_H

class PlayerbotAI;

// CoA 3.3.1 (AiPlayerbot.CoaLive.StrandedMinutes): a random bot still for that long with nothing to wait for goes back
// to the life of a free bot: out of its group, no master, its default strategies, off its mount (CoaStrandedRules).
namespace CoaStranded
{
// On the bot's update (map thread).
void Tick(PlayerbotAI* botAI);
}

#endif
