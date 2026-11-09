/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA World: rules of the bots' activity at scale (no game type, tested in tests/coa_world_activity_rules.cpp).
// The idea of "active zones" (a bot far from any real player costs little) is written anew for this module.

#ifndef PLAYERBOTS_COAWORLDACTIVITYRULES_H
#define PLAYERBOTS_COAWORLDACTIVITYRULES_H

#include <cstdint>

namespace CoaWorldActivity
{
// One key per (map, zone), for the set of zones with a real player.
inline uint64_t ZoneKey(uint32_t mapId, uint32_t zoneId) { return (uint64_t(mapId) << 32) | zoneId; }

// A bot whose AI may run less often: asleep (its activity not allowed), out of combat, no real player in its zone.
inline bool MaySlowDown(bool inCombat, bool activityAllowed, bool realPlayerInZone)
{
    return !inCombat && !activityAllowed && !realPlayerInZone;
}

// The slowed AI runs once the time waited reaches the interval (an interval of 0 runs it every tick).
inline bool SlowAIDue(uint32_t waitedMs, uint32_t intervalMs) { return waitedMs >= intervalMs; }
}  // namespace CoaWorldActivity

#endif
