/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "CoaStuckCombat.h"

#include <limits>

#include "CoaBossStandDown.h"
#include "CoaGardesRunHooks.h"
#include "CoaGroupTank.h"
#include "CombatManager.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "Log.h"
#include "Map.h"
#include "PathGenerator.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "ThreatManager.h"
#include "Timer.h"

namespace
{
    CoaStuckCombat::RunAllows RunAllowsHook;

    // What the holders of the bot's PvE combat references say.
    struct HolderScan
    {
        bool opaque = false;  // no reference at all: a combat forced by a script
        bool found = false;   // at least one legitimate holder
        float nearestDist = 0.0f;
        float nearestSeenDist = std::numeric_limits<float>::infinity();  // nearest legitimate holder in line of sight
    };

    // A path exists, full or partial, or the bot moves without one (flying, swimming, no mmaps): the conservative
    // answer, since "reachable" keeps the breaker inert.
    bool IsReachable(Player* bot, Unit* other)
    {
        PathGenerator path(bot);
        path.CalculatePath(other->GetPositionX(), other->GetPositionY(), other->GetPositionZ(), false);
        PathType const type = path.GetPathType();
        if (type & PATHFIND_NOPATH)
            return false;
        return (type & (PATHFIND_NORMAL | PATHFIND_INCOMPLETE | PATHFIND_NOT_USING_PATH)) != 0;
    }

    HolderScan ScanCombatHolders(Player* bot)
    {
        HolderScan scan;
        auto const& refs = bot->GetCombatManager().GetPvECombatRefs();
        if (refs.empty())
        {
            scan.opaque = true;
            return scan;
        }

        Map* const map = bot->GetMap();
        constexpr float radiusSq = CoaStuckCombatRule::ENGAGEMENT_RADIUS * CoaStuckCombatRule::ENGAGEMENT_RADIUS;
        for (auto const& kv : refs)
        {
            CombatReference* const ref = kv.second;
            if (!ref)
                continue;
            Unit* const other = ref->GetOther(bot);
            if (!other || !other->IsInWorld() || !other->IsAlive() || other->GetMap() != map)
                continue;
            if (other->GetCombatManager().IsInEvadeMode())
                continue;  // on its way home: not a threat
            // Range before the pathfind: a reference left behind by geometry is no fight, whatever the mesh says.
            if (bot->GetExactDistSq(other) > radiusSq)
                continue;
            Creature* const holder = other->ToCreature();
            if (CoaStuckCombatRule::IsUnresolvableCombatHolder(holder != nullptr, holder && holder->IsTrigger()))
                continue;  // a script helper nobody can fight
            if (!IsReachable(bot, other))
                continue;
            if (holder && holder->AI() && !holder->AI()->CanAIAttack(bot))
                continue;  // its own script forbids it to touch us
            float const dist = bot->GetExactDist(other);
            if (!scan.found || dist < scan.nearestDist)
                scan.nearestDist = dist;
            if (dist < scan.nearestSeenDist && bot->IsWithinLOSInMap(other))
                scan.nearestSeenDist = dist;
            scan.found = true;
        }
        return scan;
    }

    // An attacker counts only if the bot can see it: the stock attackers value drops anything out of line of sight, so
    // no action would ever fight it. Its combat reference still reaches the holder scan.
    bool HasSeenAttacker(Player* bot)
    {
        for (Unit* const attacker : bot->getAttackers())
            if (attacker && attacker->IsInWorld() && attacker->GetMap() == bot->GetMap() &&
                bot->IsWithinLOSInMap(attacker))
                return true;
        return false;
    }

    bool RaidStandDown(Player* bot, bool inRun)
    {
        return CoaBossStandDown::IsActive(bot) || (inRun && CoaGardesRunHooks::IsRaidStandDown(bot));
    }
}

void CoaStuckCombat::SetRunAllows(RunAllows hook) { RunAllowsHook = std::move(hook); }

bool CoaStuckCombat::Acts(Player* bot)
{
    if (!bot || !CoaGroupTank::ModuleGuardActs(bot, CoaStuckCombatGuard))
        return false;
    if (CoaGroupTank::InDungeonClearRun(bot))
        return !RunAllowsHook || RunAllowsHook(bot);
    return sPlayerbotAIConfig.coaCombatStuckCombatEnable;
}

void CoaBreakStuckCombatTrigger::ResetClocks()
{
    stuckSinceMs = 0;
    holderWatch.Reset();
}

bool CoaBreakStuckCombatTrigger::IsActive()
{
    if (!bot || !bot->IsInWorld() || bot->isDead() || !bot->IsInCombat() || !bot->GetMap())
    {
        ResetClocks();
        return false;
    }

    if (!CoaStuckCombat::Acts(bot) || RaidStandDown(bot, CoaGroupTank::InDungeonClearRun(bot)))
    {
        ResetClocks();
        return false;
    }

    // The two cheap reads first: a real fight almost always trips one, and the holder scan pathfinds.
    bool const hasAttacker = HasSeenAttacker(bot);
    bool const hasVictim = bot->GetVictim() != nullptr;

    bool legitimateHolder = false;
    float holderDist = 0.0f;
    float seenHolderDist = std::numeric_limits<float>::infinity();
    if (!hasAttacker && !hasVictim)
    {
        HolderScan const scan = ScanCombatHolders(bot);
        if (scan.opaque)
        {
            legitimateHolder = true;  // nothing to blame: leave a script-forced combat alone
            seenHolderDist = 0.0f;
        }
        else if (scan.found)
        {
            legitimateHolder = true;
            holderDist = scan.nearestDist;
            seenHolderDist = scan.nearestSeenDist;
        }
    }

    // The watch is re-armed only when there is nothing to measure; re-arming it while a holder closes would make the
    // first sample (always "closing") the only one.
    bool closing = false;
    if (legitimateHolder)
        closing = holderWatch.Tick(holderDist, CoaStuckCombatRule::CLOSING_STEP);
    else
        holderWatch.Reset();

    bool const hasHolder = hasAttacker || hasVictim ||
                           CoaStuckCombatRule::IsHolderProsecutingFight(legitimateHolder, seenHolderDist,
                                                                        CoaStuckCombatRule::ENGAGE_RANGE, closing);
    bool const phantom = CoaStuckCombatRule::IsPhantomCombat(true, hasAttacker, hasVictim, hasHolder);

    uint32 const timeoutMs = CoaStuckCombatRule::TimeoutMs(-1.0f, sPlayerbotAIConfig.coaCombatStuckCombatTimeoutSeconds);
    return CoaStuckCombatRule::ShouldBreakStuckCombat(phantom, getMSTime(), timeoutMs, stuckSinceMs);
}

bool CoaBreakStuckCombatAction::Execute(Event /*event*/)
{
    // Both sides, as ".combatstop": end our combat and leave every threat list, so the holder does not put the
    // reference back on its next update.
    LOG_DEBUG("playerbots", "[coa combat:{}] stuck combat: in combat with nothing to fight past the timeout, cleared",
              bot->GetName());
    bot->CombatStop();
    bot->GetThreatMgr().RemoveMeFromThreatLists();
    return true;
}
