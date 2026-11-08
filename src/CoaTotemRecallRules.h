/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_TOTEM_RECALL_RULES_H
#define COA_TOTEM_RECALL_RULES_H

#include <cstdint>

// Bots put their totems away after a fight (AiPlayerbot.CoaTotemRecall, 2.9.5), every class with totems, apart
// from the game objects, so that tests/coa_totem_recall_rules.cpp can check it.

constexpr std::uint32_t CoaTotemRecallCheckMs = 1000;
// A group chaining packs re-engages within this delay and keeps its totems.
constexpr std::uint32_t CoaTotemRecallDelayMs = 5000;

struct CoaTotemRecallMoment
{
    bool enabled;
    bool alive;
    bool inCombat;
    bool groupInCombat;  // any group member or their pet
    bool bossInProgress;
    bool inDcRun;
    bool busy;  // casting, on a taxi, in a vehicle, controlled
    std::uint32_t totemsOut;
    std::uint32_t msSinceCombat;
    bool knowsRecall;
};

enum class CoaTotemRecallDecision
{
    None,
    CastRecall,
    Unsummon
};

inline CoaTotemRecallDecision CoaTotemRecallDecide(CoaTotemRecallMoment const& m)
{
    if (!m.enabled || !m.alive || m.inCombat || m.groupInCombat || m.bossInProgress || m.inDcRun || m.busy ||
        !m.totemsOut || m.msSinceCombat < CoaTotemRecallDelayMs)
        return CoaTotemRecallDecision::None;
    return m.knowsRecall ? CoaTotemRecallDecision::CastRecall : CoaTotemRecallDecision::Unsummon;
}

#endif
