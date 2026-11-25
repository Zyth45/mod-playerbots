/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAORPHANRULES_H
#define PLAYERBOTS_COAORPHANRULES_H

// AiPlayerbot.CoaOrphanGroupReleaseDelay: a group of random bots whose player is gone (RandomPlayerbotMgr).
namespace CoaOrphanRules
{
// otherHolder: a connected member that is no random bot (a player, a selfbot, an alt or addclass bot): never orphan.
// characterOffline: a member that is a player's character, offline (the core keeps him in his raid).
// masterGone: a bot whose master is a real player out of the group (/leave, still connected).
// bornAroundPlayer (3.3.1): a real player was seen in this group; none is left now (/leave, then a logout: his bots'
// master was cleared, so masterGone no longer tells it). A group of random bots that never held a player (their
// leveling parties) is none of these and is never touched.
inline bool Orphan(bool otherHolder, bool characterOffline, bool masterGone, bool bornAroundPlayer)
{
    return !otherHolder && (characterOffline || masterGone || bornAroundPlayer);
}

// The player left it (he does not come back to it): released after 30 seconds at most, not the full delay.
inline bool Left(bool otherHolder, bool characterOffline, bool masterGone, bool bornAroundPlayer)
{
    return Orphan(otherHolder, characterOffline, masterGone, bornAroundPlayer) && !characterOffline;
}
}  // namespace CoaOrphanRules

#endif
