/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaPetStay.h"

#include "CharmInfo.h"
#include "Creature.h"
#include "Pet.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include <mutex>
#include <unordered_map>
#include <vector>

namespace
{
// Pet guid -> its stance before the stay order. Bots of several maps write it: one lock.
std::mutex lock;
std::unordered_map<ObjectGuid, ReactStates> formerStance;

std::vector<Creature*> PetsOf(Player* bot)
{
    std::vector<Creature*> pets;
    Pet* pet = bot->GetPet();
    if (pet)
        pets.push_back(pet);
    for (Unit* unit : bot->m_Controlled)
        if (Creature* creature = unit ? unit->ToCreature() : nullptr)
            if (creature != pet)
                pets.push_back(creature);
    return pets;
}

void SetStance(Creature* creature, ReactStates react)
{
    creature->SetReactState(react);
    if (CharmInfo* charmInfo = creature->GetCharmInfo())
        charmInfo->SetPlayerReactState(react);
}
}  // namespace

void CoaPetStay::Apply(Player* bot, bool stay)
{
    if (!bot || !sPlayerbotAIConfig.coaPetPassiveOnStay)
        return;

    std::lock_guard<std::mutex> guard(lock);
    // Pets dismissed while staying leave their entry behind: a bound, never reached in play.
    if (formerStance.size() > 20000)
        formerStance.clear();

    for (Creature* creature : PetsOf(bot))
    {
        ObjectGuid const guid = creature->GetGUID();
        if (stay)
        {
            if (creature->GetReactState() == REACT_PASSIVE)
                continue;
            formerStance[guid] = creature->GetReactState();
            creature->AttackStop();
            SetStance(creature, REACT_PASSIVE);
        }
        else
        {
            auto const it = formerStance.find(guid);
            if (it == formerStance.end())
                continue;
            if (creature->GetReactState() == REACT_PASSIVE)
                SetStance(creature, it->second);
            formerStance.erase(it);
        }
    }
}
