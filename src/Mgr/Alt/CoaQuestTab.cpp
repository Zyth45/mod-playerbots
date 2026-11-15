/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaQuestTab.h"

#include <ctime>
#include <map>
#include <utility>
#include <vector>

#include "AiObjectContext.h"
#include "CoaQuestTabRules.h"
#include "Creature.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "QuestDef.h"

namespace CoaQuestTab
{
namespace
{
std::string ZoneOf(Quest const* quest)
{
    int32 const zoneOrSort = quest->GetZoneOrSort();
    if (zoneOrSort > 0)
    {
        if (AreaTableEntry const* area = sAreaTableStore.LookupEntry(uint32(zoneOrSort)))
            if (area->area_name[0] && *area->area_name[0])
                return area->area_name[0];
        return "zone:" + std::to_string(zoneOrSort);
    }
    if (zoneOrSort < 0)
        return "sort:" + std::to_string(-zoneOrSort);
    return "";
}

// The line of a quest of the bot's log, empty when it is not in it.
std::string LineOf(Player* bot, uint32 questId)
{
    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    if (!quest)
        return "";
    auto const itr = bot->getQuestStatusMap().find(questId);
    if (itr == bot->getQuestStatusMap().end())
        return "";
    QuestStatus const status = itr->second.Status;
    if (status != QUEST_STATUS_INCOMPLETE && status != QUEST_STATUS_COMPLETE && status != QUEST_STATUS_FAILED)
        return "";
    std::vector<std::pair<uint32_t, uint32_t>> counts;
    for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        if (quest->RequiredNpcOrGoCount[i])
            counts.emplace_back(itr->second.CreatureOrGOCount[i], quest->RequiredNpcOrGoCount[i]);
    for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
        if (quest->RequiredItemCount[i])
            counts.emplace_back(itr->second.ItemCount[i], quest->RequiredItemCount[i]);
    return Line(questId, StateName(status == QUEST_STATUS_COMPLETE, status == QUEST_STATUS_FAILED), Objectives(counts),
                ZoneOf(quest), quest->GetTitle());
}

// The bot's log, slot order, MAX_LINES at most: quest id -> line.
std::vector<std::pair<uint32, std::string>> Log(Player* bot)
{
    std::vector<std::pair<uint32, std::string>> lines;
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE && lines.size() < MAX_LINES; ++slot)
        if (uint32 const questId = bot->GetQuestSlotQuestId(slot))
        {
            std::string line = LineOf(bot, questId);
            if (!line.empty())
                lines.emplace_back(questId, std::move(line));
        }
    return lines;
}

// The quest ender near the bot (a creature or an object), ENDER_RANGE yards at most.
Object* EnderNear(PlayerbotAI* botAI, uint32 questId)
{
    Player* bot = botAI->GetBot();
    for (ObjectGuid const guid : botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest npcs")->Get())
        if (Creature* creature = botAI->GetCreature(guid))
            if (creature->hasInvolvedQuest(questId) && bot->IsWithinDistInMap(creature, ENDER_RANGE))
                return creature;
    for (ObjectGuid const guid : botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest game objects no los")->Get())
        if (GameObject* go = botAI->GetGameObject(guid))
            if (go->hasInvolvedQuest(questId) && bot->IsWithinDistInMap(go, ENDER_RANGE))
                return go;
    return nullptr;
}

void TurnInQuest(PlayerbotAI* botAI, std::string const& arg)
{
    Player* bot = botAI->GetBot();
    uint32_t questId = 0, choice = 0;
    if (!ParseTurnIn(arg, questId, choice))
    {
        CoaAltAddon::SendToMaster(botAI, "QR\t0\terr\tunknown");
        return;
    }
    std::string const tag = "QR\t" + std::to_string(questId) + "\t";
    if (!sPlayerbotAIConfig.coaQuestLog)
    {
        CoaAltAddon::SendToMaster(botAI, tag + "err\toff");
        return;
    }
    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    QuestStatus const status = quest ? bot->GetQuestStatus(questId) : QUEST_STATUS_NONE;
    bool const inLog = status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_COMPLETE || status == QUEST_STATUS_FAILED;
    bool const complete = inLog && bot->CanRewardQuest(quest, false);
    Object* ender = complete ? EnderNear(botAI, questId) : nullptr;
    uint32 const choices = quest ? quest->GetRewChoiceItemsCount() : 0;
    uint32 const reward = choices > 1 && choice ? choice - 1 : 0;
    bool const room = complete && ender && bot->CanRewardQuest(quest, reward, false);
    TurnIn const result = Decide(inLog, complete, ender != nullptr, choices, choice, room);
    if (result == TurnIn::ChooseReward)
    {
        std::string items;
        for (uint8 i = 0; i < QUEST_REWARD_CHOICES_COUNT; ++i)
            if (quest->RewardChoiceItemId[i])
                items += (items.empty() ? "" : ",") + std::to_string(quest->RewardChoiceItemId[i]);
        CoaAltAddon::SendToMaster(botAI, tag + TurnInName(result) + "\t" + items);
        return;
    }
    if (result == TurnIn::TurnedIn)
        bot->RewardQuest(quest, reward, ender, true);
    CoaAltAddon::SendToMaster(botAI, tag + TurnInName(result));
}

void SendLog(PlayerbotAI* botAI)
{
    if (!sPlayerbotAIConfig.coaQuestLog)
    {
        CoaAltAddon::SendToMaster(botAI, "QL\terr\toff");
        return;
    }
    for (auto const& entry : Log(botAI->GetBot()))
        CoaAltAddon::SendToMaster(botAI, entry.second);
    CoaAltAddon::SendToMaster(botAI, "QL\t-");
}
}  // namespace

bool Command(PlayerbotAI* botAI, std::string const& command, std::string const& arg)
{
    if (command != "qlog" && command != "turnin" && command != "qplan")
        return false;
    if (!botAI || !botAI->GetBot())
        return true;
    if (command == "turnin")
    {
        TurnInQuest(botAI, arg);
        return true;
    }
    // The quest order is not given to the bots' travel (it is shared by every bot): the addon keeps it.
    if (command == "qplan")
    {
        CoaAltAddon::SendToMaster(botAI, "QP\terr\tlocal");
        return true;
    }
    if (arg.empty())
    {
        SendLog(botAI);
        return true;
    }
    bool on = false;
    if (!ParseWatch(arg, on))
    {
        CoaAltAddon::SendToMaster(botAI, "QL\terr\tunknown");
        return true;
    }
    if (!sPlayerbotAIConfig.coaQuestLog)
    {
        CoaAltAddon::SendToMaster(botAI, "QL\terr\toff");
        return true;
    }
    CoaAltAddon::TeamCache& cache = botAI->coaTeamCache;
    cache.qlogWatchUntil = on ? time(nullptr) + WATCH_S : 0;
    cache.qlog.clear();
    if (on)
        for (auto& entry : Log(botAI->GetBot()))
            cache.qlog.emplace(entry.first, std::move(entry.second));
    CoaAltAddon::SendToMaster(botAI, std::string("QL\twatch\t") + (on ? "1" : "0"));
    return true;
}

void TickWatch(PlayerbotAI* botAI, CoaAltAddon::TeamCache& cache)
{
    if (!sPlayerbotAIConfig.coaQuestLog || !Watching(cache.qlogWatchUntil, time(nullptr)))
    {
        cache.qlogWatchUntil = 0;
        cache.qlog.clear();
        return;
    }
    std::map<uint32, std::string> now;
    for (auto& entry : Log(botAI->GetBot()))
        now.emplace(entry.first, std::move(entry.second));
    for (auto const& [questId, line] : now)
    {
        auto const was = cache.qlog.find(questId);
        if (was == cache.qlog.end() || was->second != line)
            CoaAltAddon::SendToMaster(botAI, line);
    }
    for (auto const& [questId, line] : cache.qlog)
        if (!now.count(questId))
            CoaAltAddon::SendToMaster(botAI, "QL\tgone\t" + std::to_string(questId));
    cache.qlog.swap(now);
}
}  // namespace CoaQuestTab
