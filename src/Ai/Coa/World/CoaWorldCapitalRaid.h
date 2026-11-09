/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA World: a faction raid on a capital (CoaWorldCapitalRaid.cpp), world thread.

#ifndef PLAYERBOTS_COAWORLDCAPITALRAID_H
#define PLAYERBOTS_COAWORLDCAPITALRAID_H

#include <string>
#include <vector>

class Player;

// A raid gathering or under way at that capital ("Stormwind", "Orgrimmar": an invasion site's name).
bool CoaWorldCapitalRaidAt(std::string const& city);
// The addon's WEV? part: one WE line while a raid is on.
bool CoaWorldCapitalRaidOn();
void CoaWorldCapitalRaidLines(std::vector<std::string>& out);
// monde.json: an object, or "null" when switched off.
std::string CoaWorldCapitalRaidJson();

void AddSC_coa_world_capital_raid();

#endif
