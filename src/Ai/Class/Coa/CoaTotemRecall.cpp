/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaTotemRecall.h"

#include "CoaGroupTank.h"
#include "CoaReborn.h"
#include "CoaTotemRecallRules.h"
#include "Creature.h"
#include "Group.h"
#include "InstanceScript.h"
#include "Map.h"
#include "Pet.h"
#include "Playerbots.h"
#include "Timer.h"
#include "Totem.h"

#include <mutex>
#include <unordered_map>
#include <vector>

namespace
{
constexpr uint32 TOTEMIC_RECALL_SPELL = 36936;

struct RecallState
{
    uint32 lastCheck;
    uint32 lastCombat;
};

std::mutex stateLock;
std::unordered_map<ObjectGuid::LowType, RecallState> states;

// The time since the bot or its group was last seen in combat, or false when the check is not due yet.
bool DueForCheck(Player* bot, bool fighting, uint32& msSinceCombat)
{
    std::lock_guard<std::mutex> guard(stateLock);
    uint32 const now = getMSTime();
    auto [itr, inserted] = states.try_emplace(bot->GetGUID().GetCounter(), RecallState{now, now});
    if (fighting)
        itr->second.lastCombat = now;
    if (inserted || getMSTimeDiff(itr->second.lastCheck, now) < CoaTotemRecallCheckMs)
        return false;
    itr->second.lastCheck = now;
    msSinceCombat = getMSTimeDiff(itr->second.lastCombat, now);
    return true;
}

bool GroupInCombat(Player* bot)
{
    Group* group = bot->GetGroup();
    if (!group)
        return false;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member)
            continue;
        if (member->IsInCombat())
            return true;
        Pet* pet = member->GetPet();
        if (pet && pet->IsInCombat())
            return true;
    }
    return false;
}

bool BossInProgress(Map* map)
{
    if (!map->IsDungeon())
        return false;
    InstanceScript* instance = ((InstanceMap*)map)->GetInstanceScript();
    if (!instance)
        return false;
    for (uint32 i = 0; i < instance->GetEncounterCount(); ++i)
        if (instance->GetBossState(i) == IN_PROGRESS)
            return true;
    return false;
}

std::vector<Totem*> OwnedTotems(Player* bot, Map* map)
{
    std::vector<Totem*> totems;
    for (uint8 slot = SUMMON_SLOT_TOTEM_FIRE; slot < MAX_TOTEM_SLOT; ++slot)
    {
        ObjectGuid const guid = bot->m_SummonSlot[slot];
        if (guid.IsEmpty())
            continue;
        Creature* creature = map->GetCreature(guid);
        if (creature && creature->IsAlive() && creature->IsTotem() && creature->GetOwnerGUID() == bot->GetGUID())
            if (Totem* totem = creature->ToTotem())
                totems.push_back(totem);
    }
    return totems;
}

// The recall spell the bot really has: the stock one or its Reborn copy, 0 for none.
uint32 RecallSpell(Player* bot)
{
    if (bot->HasSpell(TOTEMIC_RECALL_SPELL))
        return TOTEMIC_RECALL_SPELL;
    uint32 const twin = RebornSpellId(TOTEMIC_RECALL_SPELL);
    return twin != TOTEMIC_RECALL_SPELL && bot->HasSpell(twin) ? twin : 0;
}
}  // namespace

void CoaTotemRecallTick(PlayerbotAI* botAI)
{
    if (!sPlayerbotAIConfig.coaTotemRecall || !botAI)
        return;
    Player* bot = botAI->GetBot();
    if (!bot || !bot->IsInWorld() || IsSelfBot(bot))
        return;
    Map* map = bot->FindMap();
    if (!map)
        return;

    bool const inCombat = bot->IsInCombat();
    bool const groupInCombat = GroupInCombat(bot);
    uint32 msSinceCombat = 0;
    if (!DueForCheck(bot, inCombat || groupInCombat, msSinceCombat))
        return;

    std::vector<Totem*> const totems = OwnedTotems(bot, map);
    if (totems.empty())
        return;

    uint32 const recall = RecallSpell(bot);
    CoaTotemRecallMoment const moment{true,
                                      bot->IsAlive(),
                                      inCombat,
                                      groupInCombat,
                                      BossInProgress(map),
                                      CoaGroupTank::InDungeonClearRun(bot),
                                      bot->IsNonMeleeSpellCast(true) || bot->IsInFlight() || bot->GetVehicle() ||
                                          bot->HasUnitState(UNIT_STATE_CONTROLLED),
                                      uint32(totems.size()),
                                      msSinceCombat,
                                      recall != 0};
    switch (CoaTotemRecallDecide(moment))
    {
        case CoaTotemRecallDecision::CastRecall:
            if (!bot->HasSpellCooldown(recall))
                bot->CastSpell(bot, recall, false);
            break;
        case CoaTotemRecallDecision::Unsummon:
            for (Totem* totem : totems)
                totem->UnSummon();
            break;
        case CoaTotemRecallDecision::None:
            break;
    }
}
