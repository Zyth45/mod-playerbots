/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COAGREETREPLYRULES_H
#define _PLAYERBOT_COAGREETREPLYRULES_H

// AiPlayerbot.CoaLive.GreetReplies.Enable: a real player's hello in /say or in the Zone channel ("i just wish if i said
// hello in the zone channel if someone said hello back", Discord, 3.0.1) gets one or two bots' hello back, by its name,
// a few seconds later. The greeting itself is the guild's rule (CoaWorldGuildRules::IsGreeting). Without the game
// objects, for the unit test.

#include <algorithm>
#include <cstdint>

namespace CoaGreetReply
{
// One answer per player and place (/say or Zone) in this many seconds: no answer to every "hi" repeated within the
// minute.
constexpr uint32_t PLAYER_CD_S = 60;
constexpr uint32_t MAX_ANSWERS = 2;

inline bool PlayerMayGreet(uint64_t lastS, uint64_t nowS) { return !lastS || nowS >= lastS + PLAYER_CD_S; }

// One or two bots (roll: any draw), never more than are there.
inline uint32_t Answers(uint32_t roll, size_t bots)
{
    return static_cast<uint32_t>(std::min<size_t>(1 + roll % MAX_ANSWERS, bots));
}

// The first answer 3 to 6 s after the hello, the second 2 to 4 s after the first: all within 10 s.
inline uint64_t FirstDueMs(uint64_t nowMs, uint32_t roll) { return nowMs + 3000 + roll % 3001; }
inline uint64_t SecondDueMs(uint64_t firstDueMs, uint32_t roll) { return firstDueMs + 2000 + roll % 2001; }

// The bots that hear a /say: within this many yards of the player.
constexpr float SAY_RANGE = 25.0f;
}  // namespace CoaGreetReply

#endif
