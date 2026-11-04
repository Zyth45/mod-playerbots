/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COAQUESTMENU_H
#define _PLAYERBOT_COAQUESTMENU_H

#include "GossipDef.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"

#include <iterator>
#include <vector>

// Whether Player::PrepareQuestMenu can list every quest of that quest giver. The core asserts once a
// quest menu holds more than GOSSIP_MAX_MENU_ITEMS: the Stormwind and Orgrimmar Call Boards of Jealous's
// mod-hero-call-board carry 47 dailies, and a bot looking them over for a quest crashed the server
// (28/09). A player never gets there - the module shows the board by category - so bots leave them be.
inline bool CoaQuestMenuFits(ObjectGuid guid)
{
    std::size_t quests = 0;
    if (guid.IsAnyTypeCreature())
    {
        auto const starts = sObjectMgr->GetCreatureQuestRelationBounds(guid.GetEntry());
        auto const ends = sObjectMgr->GetCreatureQuestInvolvedRelationBounds(guid.GetEntry());
        quests = std::distance(starts.first, starts.second) + std::distance(ends.first, ends.second);
    }
    else if (guid.IsGameObject())
    {
        auto const starts = sObjectMgr->GetGOQuestRelationBounds(guid.GetEntry());
        auto const ends = sObjectMgr->GetGOQuestInvolvedRelationBounds(guid.GetEntry());
        quests = std::distance(starts.first, starts.second) + std::distance(ends.first, ends.second);
    }
    return quests <= GOSSIP_MAX_MENU_ITEMS;
}

constexpr std::size_t CoaBigBoardQuestLimit = 25;

// The quests of a board too big for PrepareQuestMenu that an alt follows its player on, read from the quest relations
// (no quest menu is built): first the quests of this board the bot can hand in, then those its player has in the
// quest log, or handed in today (daily) or this week (weekly), so the alt catches up with the player's dailies and
// weeklies. Whether the bot may take each one is left to the usual checks (CanTakeQuest). At most
// CoaBigBoardQuestLimit quests per visit.
inline std::vector<Quest const*> CoaBigBoardQuests(Player* bot, Player* master, ObjectGuid guid)
{
    std::vector<Quest const*> quests;
    QuestRelationBounds starts;
    QuestRelationBounds ends;
    if (guid.IsAnyTypeCreature())
    {
        starts = sObjectMgr->GetCreatureQuestRelationBounds(guid.GetEntry());
        ends = sObjectMgr->GetCreatureQuestInvolvedRelationBounds(guid.GetEntry());
    }
    else if (guid.IsGameObject())
    {
        starts = sObjectMgr->GetGOQuestRelationBounds(guid.GetEntry());
        ends = sObjectMgr->GetGOQuestInvolvedRelationBounds(guid.GetEntry());
    }
    else
        return quests;

    for (auto itr = ends.first; itr != ends.second && quests.size() < CoaBigBoardQuestLimit; ++itr)
        if (Quest const* quest = sObjectMgr->GetQuestTemplate(itr->second))
            if (bot->GetQuestStatus(quest->GetQuestId()) == QUEST_STATUS_COMPLETE)
                quests.push_back(quest);

    for (auto itr = starts.first; itr != starts.second && quests.size() < CoaBigBoardQuestLimit; ++itr)
    {
        Quest const* quest = sObjectMgr->GetQuestTemplate(itr->second);
        if (!quest)
            continue;
        uint32 const questId = quest->GetQuestId();
        QuestStatus const status = master->GetQuestStatus(questId);
        bool const followed = status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_COMPLETE ||
                              (quest->IsDaily() && master->IsDailyQuestDone(questId)) ||
                              (quest->IsWeekly() && !master->SatisfyQuestWeek(quest, false));
        if (followed && bot->GetQuestStatus(questId) == QUEST_STATUS_NONE)
            quests.push_back(quest);
    }
    return quests;
}

#endif
