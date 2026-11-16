/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_DUNGEON_CLEAR_LINK_H
#define COA_DUNGEON_CLEAR_LINK_H

#include <string>

// Dungeon Clear tells SquidBots it is built in, and which version, from its script loader (AddSC), behind
// __has_include("CoaDungeonClearLink.h"):
//     CoaDungeonClearLink::Announce("3.1.4");
// SquidBots then says it once in the log and answers the addon's "SQBW\tCAPS?" (CoaDungeonClearLinkRules.h). A
// Dungeon Clear that does not announce itself is still seen through the run provider it sets on its first tick.
namespace CoaDungeonClearLink
{
// Called once, at load, before the world updates.
void Announce(char const* version);

// Dungeon Clear is built in (it announced itself or set its run provider). Meaningful from the second world tick.
bool Present();

// The version it announced; empty when it did not.
std::string Version();
}  // namespace CoaDungeonClearLink

void AddSC_coa_dungeon_clear_link();

#endif
