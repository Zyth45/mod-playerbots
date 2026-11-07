/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_GARDES_CONFIG_H
#define COA_GARDES_CONFIG_H

#include "Define.h"

#include <string>

// The settings of the combat guards that came from Dungeon Clear, read on the server's configuration with the
// player's old DungeonClear.* key first (CoaConfigAlias.h). Nothing is ever written to the player's files.
std::string CoaConfigString(std::string const& key, std::string const& alias, std::string const& fallback);
int32 CoaConfigInt(std::string const& key, std::string const& alias, int32 fallback);
bool CoaConfigBool(std::string const& key, std::string const& alias, bool fallback);

#endif
