/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

// Dungeon Clear 2.9.4 (coa/2.9.4 @ 0590afb): the GLUE of the party combat guards (the gates around the pure kernels),
// TRANSCRIBED line for line from the live code as the non-regression oracle of the module's versions. Live reads are
// replaced by the facts they produce; nothing else is changed. With a bot tank driving an active run, the module must
// give the same verdicts. Never edit to match the module.
#ifndef DC294_GLUE_H
#define DC294_GLUE_H

#include "Dc294LyceumCloseUpDecision.h"
#include "Dc294Math.h"
#include "Dc294RegroupDecision.h"

#include <cstdint>

namespace Dc294Glue
{
    // Timer.h getMSTimeDiff.
    inline uint32 getMSTimeDiff(uint32 oldMSTime, uint32 newMSTime)
    {
        if (oldMSTime > newMSTime)
            return (0xFFFFFFFF - oldMSTime) + newMSTime;
        return newMSTime - oldMSTime;
    }

    // Trigger/DungeonClearTriggers.cpp, DungeonClearRegroupCombatTrigger::IsActive (from line 2010 @ 0590afb).
    // `partyTank`: AI_VALUE(Player*, DcKey::PartyTank) non-null (the elected leader of an active, unpaused run).
    struct RegroupFacts
    {
        bool botDead = false;
        bool inCombat = true;
        bool combatRegroup = true;  // DcSettings "CombatRegroup"
        bool partyTank = true;
        bool tankIsBot = false;     // tank == bot
        bool tankSameMap = true;
        bool campHoldPassive = false;
        DcRegroupDecision::RegroupInputs in;
        float cooldownSec = 5.0f;   // DcSettings "CombatRegroupCooldown"
    };

    struct RegroupTrigger
    {
        uint32 _pendingSince = 0;
        uint32 _cooldownUntil = 0;
        bool _latched = false;

        bool IsActive(RegroupFacts const& f, uint32 now)
        {
            auto standDown = [this]() -> bool
            {
                _latched = false;
                _pendingSince = 0;
                return false;
            };
            if (f.botDead || !f.inCombat)
                return standDown();
            if (!f.combatRegroup)
                return standDown();
            if (!f.partyTank || f.tankIsBot || !f.tankSameMap)
                return standDown();
            if (f.campHoldPassive)
                return standDown();

            DcRegroupDecision::RegroupVerdict const verdict = DcRegroupDecision::DecideCombatRegroup(f.in);
            bool const wasLatched = _latched;
            if (verdict == DcRegroupDecision::RegroupVerdict::HardTether)
            {
                _latched = true;
                _pendingSince = 0;
                return true;
            }
            if (verdict == DcRegroupDecision::RegroupVerdict::None)
            {
                if (wasLatched)
                {
                    uint32 const cdMs = uint32(f.cooldownSec * 1000.0f);
                    _cooldownUntil = now + cdMs;
                    if (_cooldownUntil == 0)
                        _cooldownUntil = 1;
                }
                _latched = false;
                _pendingSince = 0;
                return false;
            }
            if (_latched)
                return true;
            bool const inCooldown = _cooldownUntil != 0 && int32_t(_cooldownUntil - now) > 0;
            if (inCooldown)
            {
                _pendingSince = 0;
                return false;
            }
            if (_pendingSince == 0)
                _pendingSince = now ? now : 1;
            if (getMSTimeDiff(_pendingSince, now) >= 2000)  // DC_REGROUP_DEBOUNCE_MS
            {
                _latched = true;
                return true;
            }
            return false;
        }
    };

    // Multiplier/DungeonClearMultiplier.cpp, LyceumChaseLeavesThePack (lines 226-241), and
    // Action/DcLyceumCloseUpAction.cpp Probe (gate), both @ 0590afb. `isDcLeader`: DcLeaderSignal::IsDungeonClearLeader.
    struct LyceumFacts
    {
        bool onBrd = true;
        bool reachAction = true;  // dynamic_cast<ReachTargetAction*>
        bool botInRoom = true;
        bool partyTank = true;
        bool tankIsBot = false;
        bool tankAlive = true;
        bool sameInstance = true;
        bool isDcLeader = false;
        bool tankInRoom = true;
        bool hasTarget = true;
        float targetX = 0, targetY = 0, tankX = 0, tankY = 0;
    };

    inline bool LyceumChaseLeavesThePack(LyceumFacts const& f)
    {
        if (!f.onBrd)
            return false;
        if (!f.reachAction || !f.botInRoom)
            return false;
        if (!f.partyTank || f.tankIsBot || !f.tankAlive || !f.sameInstance ||
            f.isDcLeader || !f.tankInRoom)
            return false;
        return f.hasTarget && DcLyceumCloseUp::ChaseLeavesThePack(f.targetX, f.targetY, f.tankX, f.tankY);
    }

    // The Probe gate (combat required), before Decide.
    inline bool LyceumProbeApplies(LyceumFacts const& f, bool inCombat)
    {
        return f.onBrd && inCombat && f.partyTank && !f.tankIsBot && f.tankAlive && f.sameInstance &&
               !f.isDcLeader && f.botInRoom && f.tankInRoom;
    }

    // Multiplier/DungeonClearMultiplier.cpp, ChaseWakesSleepingRoomBoss @ 0590afb (bot-tank branch: a real-player tank
    // returned false), plus the stand-down check at its use (line 599).
    struct RoomBossFacts
    {
        bool chaseAction = true;
        bool partyTank = true;
        bool tankAlive = true;
        bool sameInstance = true;
        bool tankHasAI = true;
        bool roomClearActive = true;
        bool hasNextBoss = true;
        bool liveBoss = true;
        bool hasTarget = true;
        bool targetIsBoss = false;
        bool bossInCombat = false;
        bool bossStandDown = false;
        float botX = 0, botY = 0, targetX = 0, targetY = 0, bossX = 0, bossY = 0, range = 0;
        float sphere = 0;  // DcEngageGeometry::RoomAggroSphereRadius(tank, boss)
    };

    inline bool ChaseWakesSleepingRoomBoss(RoomBossFacts const& f)
    {
        if (!f.chaseAction)
            return false;
        if (!f.partyTank || !f.tankAlive || !f.sameInstance)
            return false;
        if (!f.tankHasAI)
            return false;  // a real-player tank runs no room clear
        if (!f.roomClearActive)
            return false;
        if (!f.hasNextBoss)
            return false;
        if (!f.liveBoss || !f.hasTarget || f.targetIsBoss)
            return false;
        float const chaseEnd =
            DungeonClearMath::ChaseEndToBoss(f.botX, f.botY, f.targetX, f.targetY, f.bossX, f.bossY, f.range);
        return DungeonClearMath::ChaseWakesSleepingBoss(chaseEnd, f.sphere, f.bossInCombat) && !f.bossStandDown;
    }
}

#endif
