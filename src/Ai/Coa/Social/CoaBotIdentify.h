/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COABOTIDENTIFY_H
#define PLAYERBOTS_COABOTIDENTIFY_H

#include "Common.h"

#include <string>

class Group;
class Player;

// The bots of a group tell the SquidBots Lite addon that they are bots (CoaBotIdentifyRules.h).
namespace CoaBotIdentify
{
// An addon message of a player to its group (PARTY or RAID). Answers "WHO?" for every playerbot of that group of
// the asker's faction; anything else is left alone.
void OnGroupAddonMessage(Player* asker, uint32 type, std::string const& msg, Group* group);
}  // namespace CoaBotIdentify

#endif
