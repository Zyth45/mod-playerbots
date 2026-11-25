/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_FAR_MAP_RULES_H
#define COA_FAR_MAP_RULES_H

#include <cstdint>

// The far map guard of the random bots (RandomPlayerbotMgr: CoaFarMap and its callers; TravelMgr's flights), apart
// from the game objects, so that tests/coa_far_map_rules.cpp can check it. AiPlayerbot.CoaLive.FarMaps.Allow.

// 3.2.2 (the user, 24/11: "à northrend il devrait y avoir 0 bots CoA, ZERO"): map 530 (Outland, the Blood Elves' and
// Draenei's isles) and 571 (Northrend) are closed to the random bots. 3.3.1 (the user: "on bloquera, mais on mettra un
// bouton, une option ... pour débloquer"): with allowFarMaps, the whole guard is off (the refused teleports, the trip
// home, the hearth rewritten, the offline sweep, the flights there).
inline bool CoaFarMapBlocked(bool allowFarMaps, std::uint32_t mapId)
{
    return !allowFarMaps && (mapId == 530 || mapId == 571);
}

#endif
