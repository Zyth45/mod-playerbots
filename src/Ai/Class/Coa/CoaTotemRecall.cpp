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
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Totem.h"

#include <list>
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
    bool pending;
};

std::mutex stateLock;
std::unordered_map<ObjectGuid::LowType, RecallState> states;

// The time since the bot or its group was last seen in combat, or false when the check is not due yet.
bool DueForCheck(Player* bot, bool fighting, uint32& msSinceCombat)
{
    std::lock_guard<std::mutex> guard(stateLock);
    uint32 const now = getMSTime();
    auto [itr, inserted] = states.try_emplace(bot->GetGUID().GetCounter(), RecallState{now, now, true});
    if (fighting)
        itr->second.lastCombat = now;
    itr->second.pending = CoaTotemRecallPending(itr->second.pending, fighting);
    if (inserted || !itr->second.pending || getMSTimeDiff(itr->second.lastCheck, now) < CoaTotemRecallCheckMs)
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

void SetPending(Player* bot, bool pending)
{
    std::lock_guard<std::mutex> guard(stateLock);
    auto const itr = states.find(bot->GetGUID().GetCounter());
    if (itr != states.end())
        itr->second.pending = pending;
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

std::vector<Totem*> SlotTotems(Player* bot, Map* map)
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

// The CoA totems summoned outside the totem slots (Witch Doctor wards and idols, Primalist totem...).
std::vector<Creature*> UnslottedTotems(Player* bot)
{
    Acore::AnyUnitInObjectRangeCheck check(bot, CoaTotemRecallSearchRadius);
    std::list<Unit*> units;
    Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(bot, units, check);
    Cell::VisitObjects(bot, searcher, CoaTotemRecallSearchRadius);
    std::vector<Creature*> totems;
    for (Unit* unit : units)
    {
        Creature* creature = unit->ToCreature();
        if (!creature || creature->IsTotem())
            continue;
        bool const totemType = creature->GetCreatureTemplate() &&
                               creature->GetCreatureTemplate()->type == CREATURE_TYPE_TOTEM;
        if (CoaIsOwnedTotem(creature->GetOwnerGUID() == bot->GetGUID(), creature->IsAlive(), totemType))
            totems.push_back(creature);
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

    uint32 const recall = RecallSpell(bot);
    CoaTotemRecallMoment moment{true,
                                      bot->IsAlive(),
                                      inCombat,
                                      groupInCombat,
                                      BossInProgress(map),
                                      CoaGroupTank::InDungeonClearRun(bot),
                                      bot->IsNonMeleeSpellCast(true) || bot->IsInFlight() || bot->GetVehicle() ||
                                          bot->HasUnitState(UNIT_STATE_CONTROLLED),
                                      1,
                                      msSinceCombat,
                                      recall != 0};
    if (CoaTotemRecallStillPending(CoaTotemRecallDecide(moment)))
        return;

    std::vector<Totem*> const slotTotems = SlotTotems(bot, map);
    std::vector<Creature*> const otherTotems = UnslottedTotems(bot);
    moment.totemsOut = uint32(slotTotems.size() + otherTotems.size());
    moment.knowsRecall = recall && !slotTotems.empty() && !bot->HasSpellCooldown(recall);
    CoaTotemRecallDecision const decision = CoaTotemRecallDecide(moment);
    SetPending(bot, false);
    if (decision == CoaTotemRecallDecision::None)
        return;

    if (decision == CoaTotemRecallDecision::CastRecall)
        bot->CastSpell(bot, recall, false);
    else
        for (Totem* totem : slotTotems)
            totem->UnSummon();
    for (Creature* totem : otherTotems)
        totem->DespawnOrUnsummon();
}
