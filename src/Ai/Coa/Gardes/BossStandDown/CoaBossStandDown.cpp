/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "CoaBossStandDown.h"

#include "CoaGroupTank.h"
#include "CombatManager.h"
#include "Creature.h"
#include "Group.h"
#include "InstanceScript.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Timer.h"

#include <mutex>
#include <unordered_map>

namespace
{
    CoaBossStandDown::RosterBossProvider RosterBoss;

    std::mutex StatesLock;
    std::unordered_map<uint64, CoaBossStandDown::GroupState> States;

    // A group that disbanded leaves its entry behind: past this many entries, the ones not evaluated for ten minutes
    // are dropped.
    constexpr std::size_t kSweepAbove = 256;
    constexpr uint32 kStaleMs = 10 * 60 * 1000;

    InstanceScript* ScriptOf(Player* player)
    {
        Map* map = player ? player->GetMap() : nullptr;
        InstanceMap* im = map ? map->ToInstanceMap() : nullptr;
        return im ? im->GetInstanceScript() : nullptr;
    }

    bool IsRosterBoss(Player* at, Unit* unit)
    {
        if (!unit || unit->GetTypeId() != TYPEID_UNIT)
            return false;
        if (RosterBoss)
            return RosterBoss(at, unit->GetEntry());
        Creature* creature = unit->ToCreature();
        return creature && (creature->IsDungeonBoss() || creature->isWorldBoss());
    }

    // The no-script fallback: is any member on `at`'s map held in PvE combat by a roster boss?
    bool RosterBossHoldsAnyMember(Player* at)
    {
        auto const heldByBoss = [at](Player* member)
        {
            if (!member || member->GetMap() != at->GetMap())
                return false;
            for (auto const& kv : member->GetCombatManager().GetPvECombatRefs())
                if (IsRosterBoss(at, kv.second->GetOther(member)))
                    return true;
            return false;
        };

        Group* group = at->GetGroup();
        if (!group)
            return heldByBoss(at);
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            if (heldByBoss(ref->GetSource()))
                return true;
        return false;
    }

    bool EncounterSignal(Player* at)
    {
        InstanceScript* const script = ScriptOf(at);
        if (script && script->IsEncounterInProgress())
            return true;
        return RosterBossHoldsAnyMember(at);
    }

    // A five-man table boss: alive and in combat, resolved through the instance's own guid slot.
    bool TableBossEngaged(Player* at, CoaBossStandDown::TableRow const& row)
    {
        InstanceScript* const script = ScriptOf(at);
        if (!script)
            return false;
        ObjectGuid const guid = script->GetGuidData(row.guidSlot);
        if (guid.IsEmpty())
            return false;
        Creature* const boss = ObjectAccessor::GetCreature(*at, guid);
        return boss && boss->IsAlive() && boss->GetEntry() == row.bossEntry && boss->IsInCombat();
    }

    void SweepStale(uint32 now)
    {
        if (States.size() <= kSweepAbove)
            return;
        for (auto it = States.begin(); it != States.end();)
            if (now - it->second.evalMs > kStaleMs)
                it = States.erase(it);
            else
                ++it;
    }
}

void CoaBossStandDown::SetRosterBossProvider(RosterBossProvider provider)
{
    RosterBoss = std::move(provider);
}

bool CoaBossStandDown::IsActive(Player* bot)
{
    if (!bot)
        return false;
    Map* const map = bot->GetMap();
    if (!map)
        return false;
    TableRow const* const tableRow = FindTableRow(map->GetId());
    if (!map->IsRaid() && !tableRow)
        return false;
    if (!CoaGroupTank::ModuleGuardActs(bot, CoaBossStandDownGuard))
        return false;

    // The signal is read from the group's reference tank (the Dungeon Clear run leader during a run, as before), or
    // from the bot itself when the group has none on this map.
    Player* at = CoaGroupTank::Reference(bot);
    if (!at || at->GetMap() != map)
        at = bot;

    Group* const group = bot->GetGroup();
    uint64 const key = group ? group->GetGUID().GetRawValue() : bot->GetGUID().GetRawValue();
    uint32 const now = getMSTime();

    {
        std::lock_guard<std::mutex> guard(StatesLock);
        SweepStale(now);
        GroupState& s = States[key];
        Follow(s, map->GetId(), map->GetInstanceId());
        // The first member through this tick evaluates for everyone.
        if (MemoFresh(s, now))
            return s.active;
        s.evalMs = now;
    }

    // Read the world outside the lock: it touches every member's combat references.
    bool const signal = tableRow ? TableBossEngaged(at, *tableRow) : EncounterSignal(at);

    std::lock_guard<std::mutex> guard(StatesLock);
    GroupState& s = States[key];
    Follow(s, map->GetId(), map->GetInstanceId());
    Verdict const v = Update(s.active, s.signalMs, signal, now);
    s.active = v.active;
    s.signalMs = v.lastSignalMs;
    if (!s.evalMs)
        s.evalMs = now;
    return v.active;
}

bool CoaBossStandDownValue::Calculate()
{
    return CoaBossStandDown::IsActive(bot);
}
