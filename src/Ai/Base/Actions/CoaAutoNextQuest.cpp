/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaAutoNextQuest.h"

#include "Creature.h"
#include "Player.h"
#include "QuestDef.h"

bool CoaTakeAutoNextQuest(Player* bot, Quest const* quest, Object* questGiver)
{
    if (!bot || !quest || !questGiver)
        return false;
    bool const creature = questGiver->GetTypeId() == TYPEID_UNIT;
    Quest const* next = creature ? bot->GetNextQuest(questGiver->GetGUID(), quest) : nullptr;
    if (!CoaTakesAutoNextQuest(creature, next != nullptr, next && next->IsAutoAccept(),
                               next && bot->CanTakeQuest(next, false), next && bot->CanAddQuest(next, false)))
        return false;
    bot->AddQuestAndCheckCompletion(next, questGiver);
    return true;
}
