/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_LFG_RULES_H
#define COA_LFG_RULES_H

#include <cstdint>

// How a bot answers a dungeon finder proposal (LfgAcceptAction), apart from the game objects, so that
// tests/coa_lfg_rules.cpp can check it. A bot in combat or dead used to decline, which cancelled the dungeon for the
// whole group: it never declines now, it waits and accepts once free, or anyway just before the proposal expires.
enum class CoaLfgProposalAnswer
{
    Accept,
    Wait,
    DoNotAnswer  // a real player's character, or a bot in a battleground or arena
};

// Accepted at the latest this long before the proposal expires (the bot answers on its next AI tick).
constexpr std::uint32_t COA_LFG_PROPOSAL_MARGIN_MS = 8000;

inline CoaLfgProposalAnswer CoaLfgProposalDecision(bool realPlayer, bool inBattlegroundOrArena, bool inCombat,
                                                   bool dead, std::uint32_t waitedMs, std::uint32_t proposalMs)
{
    if (realPlayer || inBattlegroundOrArena)
        return CoaLfgProposalAnswer::DoNotAnswer;
    if (!inCombat && !dead)
        return CoaLfgProposalAnswer::Accept;
    std::uint32_t const deadline = proposalMs > COA_LFG_PROPOSAL_MARGIN_MS ? proposalMs - COA_LFG_PROPOSAL_MARGIN_MS : 0;
    return waitedMs >= deadline ? CoaLfgProposalAnswer::Accept : CoaLfgProposalAnswer::Wait;
}

// Past this the proposal is gone (expired, or ended without an update reaching the bot): forget it, never answer it.
inline bool CoaLfgProposalExpired(std::uint32_t waitedMs, std::uint32_t proposalMs)
{
    return waitedMs > proposalMs;
}

// 3.2.1 (AiPlayerbot.CoaLfgRolePriority): a queued party of real players misses a tank (or a healer) no queued bot
// covers. A free bot of that role queues at its next check (a few seconds) instead of waiting for its random roll (one
// chance in 20 a check): the missing role comes first. tank, heal: the bot would queue as one.
inline bool CoaLfgRoleWanted(bool tank, bool heal, bool needTank, bool needHeal)
{
    return (tank && needTank) || (heal && needHeal);
}

// 3.2.2 (Dungeon Clear, issue 62): the classes the addclass cache holds (the queue fill and .playerbots addclass read it):
// every class below maxClasses (the core's MAX_CLASSES, the CoA classes up to 32 included), none 0 or 10 (unused).
// The cache stopped at the druid while the AddClass accounts hold CoA classes only: it was empty.
inline bool CoaAddclassCacheClass(std::uint8_t claz, std::uint8_t maxClasses)
{
    return claz != 0 && claz != 10 && claz < maxClasses;
}

#endif
