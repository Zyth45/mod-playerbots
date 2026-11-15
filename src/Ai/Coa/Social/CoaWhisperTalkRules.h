/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COAWHISPERTALKRULES_H
#define _PLAYERBOT_COAWHISPERTALKRULES_H

#include <cstdint>

// A random bot's talk with one real player by whisper (AiPlayerbot.CoaLive.WhisperEscape.Enable, CoA Bots 3.1.4).
// The bot answers a few whispers, then gives a plausible excuse ("dungeon queue popped", "bg just popped") and stays
// silent a while for that player: a real player does not chat forever with a stranger. Pure rules, tested alone.
namespace CoaWhisperTalk
{
constexpr uint64_t RESET_MS = 10 * 60 * 1000;        // no whisper for 10 min: the talk starts again from zero
constexpr uint32_t MIN_ANSWERS = 5;                   // answers before the excuse, drawn per pair in [5, 8]
constexpr uint32_t MAX_ANSWERS = 8;
constexpr uint32_t SILENCE_MIN_MS = 20 * 60 * 1000;  // the silence after the excuse, drawn in [20, 40] min
constexpr uint32_t SILENCE_MAX_MS = 40 * 60 * 1000;
constexpr uint32_t PRUNE_ABOVE = 512;                 // pairs kept before the old ones are dropped

// The talk of one bot with one player.
struct Pair
{
    uint64_t lastMs = 0;         // the last whisper of the player to this bot
    uint32_t answers = 0;        // whispers answered since the talk started
    uint32_t limit = 0;          // answers before the excuse
    uint64_t silentUntilMs = 0;  // the excuse was given: no answer until then
};

enum class Step
{
    Answer,  // the usual answer
    Escape,  // the excuse, then the silence
    Silent   // no answer: the player insists after the excuse
};

// The step for a whisper of the player at nowMs. limitIfNew and silenceMs: drawn by the caller, used when a talk
// starts and when the excuse is given. Insisting does not lengthen the silence.
inline Step Next(Pair& pair, uint64_t nowMs, uint32_t limitIfNew, uint32_t silenceMs)
{
    if (pair.silentUntilMs)
    {
        if (nowMs < pair.silentUntilMs)
        {
            pair.lastMs = nowMs;
            return Step::Silent;
        }
        pair = Pair();
    }
    if (!pair.lastMs || nowMs - pair.lastMs >= RESET_MS)
    {
        pair.answers = 0;
        pair.limit = limitIfNew;
    }
    pair.lastMs = nowMs;
    if (pair.answers >= pair.limit)
    {
        pair.answers = 0;
        pair.silentUntilMs = nowMs + silenceMs;
        return Step::Escape;
    }
    ++pair.answers;
    return Step::Answer;
}

// A pair that says nothing any more: its talk is over and its silence too.
inline bool Stale(Pair const& pair, uint64_t nowMs)
{
    return nowMs >= pair.silentUntilMs && (!pair.lastMs || nowMs - pair.lastMs >= RESET_MS);
}

// The excuse that fits what the bot is doing, so that it is never a visible lie: busy when it is already in a
// dungeon, a battleground, a fight or a group; away below level 10 (no battleground yet); a battleground from level
// 10; a dungeon or a battleground (coin) from level 15.
inline char const* EscapeSituation(uint32_t level, bool inInstance, bool inCombat, bool inGroup, bool coin)
{
    if (inInstance || inCombat || inGroup)
        return "wh_escape_busy";
    if (level < 10)
        return "wh_escape_afk";
    if (level < 15 || !coin)
        return "wh_escape_bg";
    return "wh_escape_dungeon";
}
}  // namespace CoaWhisperTalk

#endif
