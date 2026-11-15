/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAQUESTTAB_H
#define PLAYERBOTS_COAQUESTTAB_H

#include <string>

#include "CoaAltAddon.h"

class PlayerbotAI;

// The group Quests tab of the SquidBots addon (CoaQuestTabRules.h): "qlog", "qlog watch on|off", "turnin", "qplan",
// answered from the alt's queue (CoaAltBags::Enqueue/Drain).
namespace CoaQuestTab
{
// True when the command is one of these (it is then answered, even when the option is off).
bool Command(PlayerbotAI* botAI, std::string const& command, std::string const& arg);

// From CoaAltAddon::TickTeam (every 5 s, alts of a player with the addon): the watched log's changes.
void TickWatch(PlayerbotAI* botAI, CoaAltAddon::TeamCache& cache);
}

#endif
