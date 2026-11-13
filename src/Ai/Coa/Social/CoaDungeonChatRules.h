/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COADUNGEONCHATRULES_H
#define _PLAYERBOT_COADUNGEONCHATRULES_H

// AiPlayerbot.CoaLive.DungeonChat.Enable: the bots of a real player's dungeon group talk in party chat, a little ("en
// donjon j'ai trop peu de msg en /p ... un petit bonjour en entrée de dj, un msg par-ci par-là entre chaque boss ou
// avant un pull, et un msg final", 3.0.1): two bots say hello when the player enters, one line now and then out of a
// fight, two bots say thanks or "another one?" when it leaves. Without the game objects, for the unit test.

#include <algorithm>
#include <cstdint>

namespace CoaDungeonChat
{
// Two bots at the entrance and at the end, fewer only when the group has fewer.
constexpr uint32_t ANSWERS = 2;
inline uint32_t Speakers(size_t bots) { return static_cast<uint32_t>(std::min<size_t>(ANSWERS, bots)); }

// A line now and then: not in the first 2 minutes, then one every IDLE_MIN_MS + up to IDLE_JITTER_MS, never while a
// member of the group fights.
constexpr uint64_t FIRST_IDLE_MS = 2 * 60 * 1000;
constexpr uint64_t IDLE_MIN_MS = 4 * 60 * 1000;
constexpr uint64_t IDLE_JITTER_MS = 2 * 60 * 1000;

inline uint64_t NextIdleMs(uint64_t fromMs, uint32_t roll) { return fromMs + IDLE_MIN_MS + roll % (IDLE_JITTER_MS + 1); }
inline uint64_t FirstIdleMs(uint64_t enterMs, uint32_t roll) { return enterMs + FIRST_IDLE_MS + roll % (IDLE_JITTER_MS + 1); }
inline bool IdleDue(uint64_t nextIdleMs, uint64_t nowMs, bool groupInCombat) { return !groupInCombat && nowMs >= nextIdleMs; }

// The end: only after a real run (5 minutes in the instance), and once per entrance.
constexpr uint64_t END_AFTER_MS = 5 * 60 * 1000;
inline bool EndDue(uint64_t enterMs, uint64_t nowMs) { return nowMs >= enterMs + END_AFTER_MS; }

// The hello again only for a new entrance: back in the same instance within this long (a death, a hearth to repair),
// no second hello.
constexpr uint64_t REENTER_MS = 30 * 60 * 1000;
inline bool NewEntrance(uint32_t lastInstance, uint64_t lastEnterMs, uint32_t instance, uint64_t nowMs)
{
    return lastInstance != instance || nowMs >= lastEnterMs + REENTER_MS;
}

// The two lines of the entrance and of the end: the first 3 to 6 s on, the second 3 to 10 s after it.
inline uint64_t FirstDueMs(uint64_t nowMs, uint32_t roll) { return nowMs + 3000 + roll % 3001; }
inline uint64_t NextDueMs(uint64_t previousDueMs, uint32_t roll) { return previousDueMs + 3000 + roll % 7001; }
}  // namespace CoaDungeonChat

#endif
