/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COAGREETREPLYRULES_H
#define _PLAYERBOT_COAGREETREPLYRULES_H

// AiPlayerbot.CoaLive.GreetReplies.Enable: a real player's hello in /say, in party chat, in the Zone channel or in the
// Ascension (world) channel ("i just wish if i said hello in the zone channel if someone said hello back", Discord,
// 3.0.1) gets two bots' hello back, by its name, a few seconds apart ("toujours 2 bots"; one only when one bot is
// there). The greeting itself is the guild's rule (CoaWorldGuildRules::IsGreeting). Without the game objects, for the
// unit test.

#include <algorithm>
#include <cstdint>

namespace CoaGreetReply
{
// One answer per player and place in this many seconds: no answer to every "hi" repeated within the minute.
constexpr uint32_t PLAYER_CD_S = 60;
// A public channel answers one hello in this many seconds, whoever says it: several players saying hello in the
// world channel at once never set off a burst of bots.
constexpr uint32_t CHANNEL_GAP_S = 10;
constexpr uint32_t ANSWERS = 2;

inline bool PlayerMayGreet(uint64_t lastS, uint64_t nowS) { return !lastS || nowS >= lastS + PLAYER_CD_S; }
inline bool ChannelMayGreet(uint64_t lastS, uint64_t nowS) { return !lastS || nowS >= lastS + CHANNEL_GAP_S; }

// Two bots, fewer only when fewer are there.
inline uint32_t Answers(size_t bots) { return static_cast<uint32_t>(std::min<size_t>(ANSWERS, bots)); }

// The first answer 3 to 6 s after the hello, the second 3 to 10 s after the first.
inline uint64_t FirstDueMs(uint64_t nowMs, uint32_t roll) { return nowMs + 3000 + roll % 3001; }
inline uint64_t NextDueMs(uint64_t previousDueMs, uint32_t roll) { return previousDueMs + 3000 + roll % 7001; }

// The bots that hear a /say: within this many yards of the player.
constexpr float SAY_RANGE = 25.0f;
}  // namespace CoaGreetReply

#endif
