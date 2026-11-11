/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "STTriggers.h"

#include <list>
#include <map>
#include <mutex>
#include <unordered_set>

#include "CoaGroupTank.h"
#include "Creature.h"
#include "GameObject.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"

namespace
{
    using namespace SunkenTemple;

    // Keyed by instance id; the sets' contents are only touched by their own instance's map thread, the map structure
    // by every Sunken Temple thread, hence the lock. Callers copy a set under it and scan the copy.
    std::mutex Lock;
    std::map<uint32, std::unordered_set<ObjectGuid>> Doused;
    std::map<uint32, std::unordered_set<ObjectGuid>> Harvested;

    std::unordered_set<ObjectGuid> Copy(std::map<uint32, std::unordered_set<ObjectGuid>>& registry, uint32 instanceId)
    {
        std::lock_guard<std::mutex> guard(Lock);
        auto itr = registry.find(instanceId);
        return itr == registry.end() ? std::unordered_set<ObjectGuid>() : itr->second;
    }

    bool EncounterLive(Player* bot)
    {
        return bot->FindNearestCreature(NPC_SHADE_OF_HAKKAR, ENCOUNTER_SCAN, true) ||
               bot->FindNearestCreature(NPC_HAKKARI_SUPPRESSOR, ENCOUNTER_SCAN, true);
    }
}

bool SunkenTemple::HakkarEncounterActs(Player* bot)
{
    if (!bot || !bot->IsAlive() || bot->GetMapId() != MAP_ID)
        return false;
    if (!CoaGroupTank::RunOrKey(bot, sPlayerbotAIConfig.coaCombatSunkenTempleEnable))
        return false;
    if (EncounterLive(bot))
        return true;

    // Over (or not started): the next pull of the same flames and corpses (their GUIDs are stable) starts clean.
    std::lock_guard<std::mutex> guard(Lock);
    Doused.erase(bot->GetInstanceId());
    Harvested.erase(bot->GetInstanceId());
    return false;
}

// A Suppressor's drain is an out-of-combat event: once tagged it is neutralised and normal combat finishes it, so only
// one still channelling is worth the peel.
Creature* SunkenTemple::ChannellingSuppressor(Player* bot)
{
    std::list<Creature*> suppressors;
    bot->GetCreatureListWithEntryInGrid(suppressors, NPC_HAKKARI_SUPPRESSOR, ENCOUNTER_SCAN);
    Creature* best = nullptr;
    float bestDist = 0.0f;
    for (Creature* c : suppressors)
    {
        if (!c || !c->IsAlive() || c->IsInCombat())
            continue;
        float const d = bot->GetDistance(c);
        if (!best || d < bestDist)
        {
            best = c;
            bestDist = d;
        }
    }
    return best;
}

GameObject* SunkenTemple::FlameToDouse(Player* bot)
{
    std::unordered_set<ObjectGuid> const done = Copy(Doused, bot->GetInstanceId());
    GameObject* best = nullptr;
    float bestDist = 0.0f;
    for (uint32 entry : GO_ETERNAL_FLAMES)
    {
        GameObject* go = bot->FindNearestGameObject(entry, ENCOUNTER_SCAN);
        if (!go || done.count(go->GetGUID()))
            continue;
        float const d = bot->GetDistance(go);
        if (!best || d < bestDist)
        {
            best = go;
            bestDist = d;
        }
    }
    return best;
}

Creature* SunkenTemple::BloodkeeperToHarvest(Player* bot)
{
    std::unordered_set<ObjectGuid> const done = Copy(Harvested, bot->GetInstanceId());
    std::list<Creature*> keepers;
    bot->GetCreatureListWithEntryInGrid(keepers, NPC_HAKKARI_BLOODKEEPER, ENCOUNTER_SCAN);
    Creature* best = nullptr;
    float bestDist = 0.0f;
    for (Creature* c : keepers)
    {
        if (!c || c->IsAlive() || done.count(c->GetGUID()))
            continue;
        float const d = bot->GetDistance(c);
        if (!best || d < bestDist)
        {
            best = c;
            bestDist = d;
        }
    }
    return best;
}

void SunkenTemple::MarkDoused(Player* bot, GameObject* flame)
{
    std::lock_guard<std::mutex> guard(Lock);
    Doused[bot->GetInstanceId()].insert(flame->GetGUID());
}

void SunkenTemple::MarkHarvested(Player* bot, Creature* keeper)
{
    std::lock_guard<std::mutex> guard(Lock);
    Harvested[bot->GetInstanceId()].insert(keeper->GetGUID());
}

bool StHakkarSuppressorTrigger::IsActive()
{
    return SunkenTemple::HakkarEncounterActs(bot) && SunkenTemple::ChannellingSuppressor(bot);
}

bool StHakkarFlameTrigger::IsActive()
{
    return SunkenTemple::HakkarEncounterActs(bot) && bot->HasItemCount(SunkenTemple::ITEM_HAKKARI_BLOOD, 1) &&
           SunkenTemple::FlameToDouse(bot);
}

bool StHakkarBloodTrigger::IsActive()
{
    return SunkenTemple::HakkarEncounterActs(bot) && SunkenTemple::BloodkeeperToHarvest(bot);
}
