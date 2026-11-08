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

#endif
