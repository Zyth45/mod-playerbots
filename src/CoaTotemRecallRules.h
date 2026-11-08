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
// CoA classes (Witch Doctor wards and idols, Primalist totem...) summon their totems as plain summons, out of the
// four totem slots: they are looked for around the bot, once per fight end.
constexpr float CoaTotemRecallSearchRadius = 60.0f;

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

// Once per fight end: pending is set while the bot or its group fights, and cleared once the totems were put
// away or none was found. A moment that blocks the recall (boss, Dungeon Clear run, busy) keeps it pending.
inline bool CoaTotemRecallPending(bool pending, bool fighting)
{
    return pending || fighting;
}

inline bool CoaTotemRecallStillPending(CoaTotemRecallDecision decisionWithTotems)
{
    return decisionWithTotems == CoaTotemRecallDecision::None;
}

// A creature the bot put down as a totem: owned by it, alive, a totem by its creature type or its class.
inline bool CoaIsOwnedTotem(bool ownedByBot, bool alive, bool totemType)
{
    return ownedByBot && alive && totemType;
}

#endif
