/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
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
