/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAIMMERSIONRULES_H
#define PLAYERBOTS_COAIMMERSIONRULES_H

#include <cstdint>
#include <functional>

// CoA Live, L2 Immersion (AiPlayerbot.CoaLive.Immersion.Enable): a random bot idling in the open world near water
// sits down on the shore and fishes for a while, as players do between two quests. Pure rules.
namespace CoaImmersion
{
struct BotView
{
    bool randomBot = true;
    bool ownedByPlayer = false;  // a real player's bot or in a real player's group
    bool alive = true;
    bool inCombat = false;
    bool inGroup = false;
    bool inInstanceOrCity = false;
    bool idleStatus = true;      // the "new rpg" has it idle, resting or wandering around
    bool knowsFishing = false;
};

constexpr uint32_t START_CHANCE_PER_MILLE = 25;  // per look (about every 5 s): a few minutes of idling on average

inline bool MayStartFishing(BotView const& b)
{
    return b.randomBot && !b.ownedByPlayer && b.alive && !b.inCombat && !b.inGroup && !b.inInstanceOrCity &&
           b.idleStatus && b.knowsFishing;
}

// The session at the water, in ms: 4 to 15 minutes.
inline uint32_t FishingMs(std::function<uint32_t(uint32_t, uint32_t)> const& between)
{
    return between(4 * 60, 15 * 60) * 1000;
}

// Stop fishing: time is up, a fight, a group, or the bot was sent elsewhere.
inline bool MustStop(uint64_t nowMs, uint64_t endMs, bool inCombat, bool inGroup, bool stillResting)
{
    return nowMs >= endMs || inCombat || inGroup || !stillResting;
}
}  // namespace CoaImmersion

#endif
