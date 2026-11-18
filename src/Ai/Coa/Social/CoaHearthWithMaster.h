/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAHEARTHWITHMASTER_H
#define PLAYERBOTS_COAHEARTHWITHMASTER_H

class PlayerbotAI;

// CoA 3.1.6 (AiPlayerbot.CoaHearthWithMaster): when its master uses his hearthstone, a bot of his group standing
// near him, out of combat, with its own hearthstone ready, casts it too. When the master is gone, the bot stops
// its cast, its hearthstone goes on cooldown as if used, and it arrives a moment after him where he arrived.
// A bot whose hearthstone is on cooldown stays where it is, as before.
namespace CoaHearthWithMaster
{
// The bot's update, before its cast in progress is waited for (map thread).
void Tick(PlayerbotAI* botAI);
}  // namespace CoaHearthWithMaster

#endif
