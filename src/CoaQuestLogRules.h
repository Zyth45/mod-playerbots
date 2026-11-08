/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_QUEST_LOG_RULES_H
#define COA_QUEST_LOG_RULES_H

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

// How a random bot tidies a nearly full quest log (NewRpgBaseAction::OrganizeQuestLog), apart from the game objects,
// so that tests/coa_quest_log_rules.cpp can check it. The old tidy-up dropped every quest outside the bot's zone and
// then wiped the whole log: each quest is now scored and only the worst few are dropped.
struct CoaQuestKeepInfo
{
    bool templateMissing = false;
    bool failed = false;
    bool complete = false;  // ready to turn in
    bool worthDoing = true;
    bool capableDoing = true;
    float progress = -1.0f;  // objectives done, 0..1; below 0 when the quest has no counted objective
    bool inBotZone = false;
    bool sortBucket = false;  // class, profession or seasonal sort instead of a zone
    bool hasPoi = true;
    std::int32_t levelGap = 0;  // bot level - quest level
    bool lowPriority = false;   // already given up on
};

// Higher is kept longer.
inline float CoaQuestKeepScore(CoaQuestKeepInfo const& q)
{
    if (q.templateMissing)
        return -10000.0f;
    if (q.failed)
        return -1000.0f;
    if (q.complete)
        return 1000.0f;

    float score = 0.0f;
    if (!q.worthDoing)
        score -= 500.0f;
    if (!q.capableDoing)
        score -= 400.0f;
    if (q.progress > 0.0f)
        score += 300.0f * std::min(q.progress, 1.0f);
    if (q.inBotZone)
        score += 150.0f;
    else if (q.sortBucket)
        score -= 50.0f;
    if (!q.hasPoi)
        score -= 200.0f;
    score -= std::abs(q.levelGap) * 5.0f;
    if (q.lowPriority)
        score -= 600.0f;
    return score;
}

struct CoaQuestLogEntry
{
    std::uint16_t slot = 0;
    float score = 0.0f;
    bool complete = false;
};

// The log slots to drop this pass: lowest scores first, at most maxDrops (never less than one), and never a quest
// that is ready to turn in, even when the log holds nothing else.
inline std::vector<std::uint16_t> CoaQuestSlotsToDrop(std::vector<CoaQuestLogEntry> entries, std::uint32_t maxDrops)
{
    if (maxDrops < 1)
        maxDrops = 1;
    std::stable_sort(entries.begin(), entries.end(),
                     [](CoaQuestLogEntry const& a, CoaQuestLogEntry const& b) { return a.score < b.score; });
    std::vector<std::uint16_t> slots;
    for (CoaQuestLogEntry const& e : entries)
    {
        if (slots.size() >= maxDrops)
            break;
        if (e.complete)
            continue;
        slots.push_back(e.slot);
    }
    return slots;
}

#endif
