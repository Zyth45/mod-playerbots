/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef COA_THREAT_LEAD_SETTINGS_H
#define COA_THREAT_LEAD_SETTINGS_H

#include "Define.h"

class Player;

// The settings of the threat lead and of the assist on the tank's fight, per map tier (normal, heroic tier, raid),
// with Dungeon Clear's defaults and the player's old DungeonClear.* keys read first (CoaGardesConfig). Read once, on
// the first call: these are read on every damaging action of every bot, and a configuration lookup per call would
// cost a scan of every key. A change needs a restart.
namespace CoaThreatLeadSettings
{
    // How long the damage dealers wait after the tank's first hit, in ms (0 = the lead is off).
    uint32 LeadMs(Player const* member);
    // Under this health percent of the tank, nobody waits (0 = never).
    float PanicHp(Player const* member);
    // How long "the group is fighting" stays on after the last read that saw it, in ms.
    uint32 LatchMs(Player const* member);
    // How far from the tank a bot's combat still counts as the tank's fight, in yards.
    float Spread(Player const* member);
}

#endif
