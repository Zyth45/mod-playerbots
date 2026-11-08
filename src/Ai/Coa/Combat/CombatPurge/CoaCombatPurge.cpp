/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "CoaCombatPurge.h"

#include <map>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "AiObjectContext.h"
#include "CoaBossStandDown.h"
#include "CoaCombatPurgeRule.h"
#include "CoaGardesRunHooks.h"
#include "CoaGroupTank.h"
#include "CombatManager.h"
#include "Creature.h"
#include "Group.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "ThreatManager.h"
#include "Timer.h"

namespace
{
    // Per bot, world thread only: how long a listed creature has held it.
    std::unordered_map<ObjectGuid, uint32> HeldMs;

    // Post-purge target bars, (instance id, entry) -> getMSTime() at which the bar ends. Keyed by instance: every bot
    // of the instance must get the same answer, and the reader is whichever bot is picking a target (a map thread).
    std::map<std::pair<uint32, uint32>, uint32> Bars;
    std::mutex BarsLock;

    void ArmBar(uint32 instanceId, uint32 entry, uint32 untilMs)
    {
        std::lock_guard<std::mutex> lock(BarsLock);
        uint32& slot = Bars[{instanceId, entry}];
        if (slot == 0 || static_cast<int32>(untilMs - slot) > 0)
            slot = untilMs;
    }

    bool InRaidStandDown(Player* bot)
    {
        return CoaBossStandDown::IsActive(bot) ||
               (CoaGroupTank::InDungeonClearRun(bot) && CoaGardesRunHooks::IsRaidStandDown(bot));
    }

    bool GuardActs(Player* bot)
    {
        if (!CoaGroupTank::GuardScope(bot))
            return false;
        return CoaGroupTank::InDungeonClearRun(bot) || sPlayerbotAIConfig.coaCombatCombatPurgeEnable;
    }

    bool IsListed(Unit* u, uint32 mapId)
    {
        if (!u || !u->IsInWorld())
            return false;
        Creature* creature = u->ToCreature();
        return creature && CoaCombatPurgeRule::IsPurgeable(mapId, creature->GetEntry(),
                                                           creature->GetCombatManager().IsInEvadeMode());
    }

    // Is a listed creature holding this member? The holders live in the combat manager: getAttackers() only has the
    // units whose current victim is the member, and a stranded holder has none. The attackers stay as a superset.
    bool HeldByListed(Player* member)
    {
        uint32 const mapId = member->GetMapId();
        for (auto const& kv : member->GetCombatManager().GetPvECombatRefs())
            if (CombatReference* const ref = kv.second)
                if (IsListed(ref->GetOther(member), mapId))
                    return true;
        for (Unit* const attacker : member->getAttackers())
            if (IsListed(attacker, mapId))
                return true;
        return false;
    }
}

bool CoaCombatPurge::IsBarred(Player* bot, uint32 entry)
{
    if (!bot)
        return false;
    uint32 const now = getMSTime();
    std::lock_guard<std::mutex> lock(BarsLock);
    auto const it = Bars.find({bot->GetInstanceId(), entry});
    if (it == Bars.end())
        return false;
    if (static_cast<int32>(it->second - now) <= 0)
    {
        Bars.erase(it);
        return false;
    }
    return true;
}

uint32 CoaCombatPurge::Purge(Player* member)
{
    if (!member || !member->IsInWorld())
        return 0;
    uint32 const mapId = member->GetMapId();
    if (!CoaCombatPurgeRule::HasRowsFor(mapId))
        return 0;

    // The party on the member's map, humans included: the holder flags them too, and a flagged human keeps the party
    // gates shut as well. Nothing below acts on a member's own combat; only the creature is stopped.
    std::vector<Player*> party{member};
    if (Group* group = member->GetGroup())
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* other = ref->GetSource();
            if (other && other != member && other->IsInWorld() && other->GetMapId() == mapId)
                party.push_back(other);
        }

    // Collect before clearing: EndCombat deletes the reference being walked and CombatStop changes the attacker set.
    std::vector<Creature*> doomed;
    std::unordered_set<uint64> seen;
    auto const consider = [&doomed, &seen, mapId](Unit* u)
    {
        if (!u || !u->IsInWorld() || !seen.insert(u->GetGUID().GetRawValue()).second)
            return;
        if (IsListed(u, mapId))
            doomed.push_back(u->ToCreature());
    };
    for (Player* p : party)
    {
        for (auto const& kv : p->GetCombatManager().GetPvECombatRefs())
            if (CombatReference* const ref = kv.second)
                consider(ref->GetOther(p));
        for (Unit* const attacker : p->getAttackers())
            consider(attacker);
    }
    if (doomed.empty())
        return 0;

    uint32 const now = getMSTime();
    uint32 const barMs = sPlayerbotAIConfig.coaCombatCombatPurgeWindowMs;
    std::unordered_set<uint64> dropped;
    uint32 cleared = 0;
    for (Creature* creature : doomed)
    {
        if (!creature->IsInWorld())
            continue;
        LOG_DEBUG("playerbots", "[coa combat:{}] combat purge: dropping {} ({}), {:.0f} yd away",
                  member->GetName(), creature->GetName(), creature->GetEntry(), member->GetDistance(creature));
        dropped.insert(creature->GetGUID().GetRawValue());
        ArmBar(member->GetInstanceId(), creature->GetEntry(), now + barMs);
        // The creature's side, or its threat reference flags the party again on its next update. Stopping the
        // creature ends the reference for every member at once.
        creature->GetThreatMgr().ClearAllThreat();
        creature->CombatStop(true);
        ++cleared;
    }
    if (!cleared)
        return 0;

    // CombatStop leaves the bots' targets set, and a bot still pointed at the creature would attack it again on its
    // next tick. Bots only: a human's target is his own.
    for (Player* p : party)
    {
        PlayerbotAI* ai = GET_PLAYERBOT_AI(p);
        if (!ai)
            continue;
        AiObjectContext* ctx = ai->GetAiObjectContext();
        Unit* const victim = p->GetVictim();
        Unit* const picked = ctx ? ctx->GetValue<Unit*>("current target")->Get() : nullptr;
        bool const onDropped = (victim && dropped.count(victim->GetGUID().GetRawValue())) ||
                               (picked && dropped.count(picked->GetGUID().GetRawValue()));
        if (!onDropped)
            continue;
        p->AttackStop();
        p->SetTarget();
        if (ctx)
            ctx->GetValue<Unit*>("current target")->Set(nullptr);
    }
    return cleared;
}

void CoaCombatPurge::Tick(uint32 diffMs)
{
    uint32 const windowMs = sPlayerbotAIConfig.coaCombatCombatPurgeWindowMs;
    for (auto const& [guid, bot] : ObjectAccessor::GetPlayers())
    {
        // Cheapest first: only maps with a row, only bots in combat. Not gated on being alive: a member held by a
        // listed creature after its death still keeps the party from resting and resurrecting.
        if (!bot || !bot->IsInWorld() || !CoaCombatPurgeRule::HasRowsFor(bot->GetMapId()) || !bot->IsInCombat() ||
            !GET_PLAYERBOT_AI(bot) || !GuardActs(bot) || InRaidStandDown(bot))
        {
            HeldMs.erase(guid);
            continue;
        }
        uint32& held = HeldMs[guid];
        if (CoaCombatPurgeRule::Step(held, diffMs, HeldByListed(bot), windowMs))
            Purge(bot);
    }
}
