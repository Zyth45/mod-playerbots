/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaAltAddon.h"

#include "ChatHelper.h"
#include "DisableMgr.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "QuestDef.h"
#include "ScriptMgr.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <set>
#include <sstream>
#include <unordered_map>

namespace
{
constexpr time_t SEEN_FOR = 15 * MINUTE;
constexpr uint32 MAX_CHAIN = 25;
constexpr uint32 MAX_DEPTH = 40;
constexpr size_t MAX_IDS = 12;

std::mutex seenLock;
std::unordered_map<uint64, time_t> seenAt;

bool InLog(QuestStatus status)
{
    return status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_COMPLETE || status == QUEST_STATUS_FAILED;
}

// Everything CanTakeQuest checks but the chain: previous quests and a previous quest still in the log.
bool BlockedOutsideChain(Player* bot, Quest const* quest)
{
    return !sScriptMgr->OnPlayerCanTakeQuest(bot, quest) ||
           sDisableMgr->IsDisabledFor(DISABLE_TYPE_QUEST, quest->GetQuestId(), bot) ||
           !bot->SatisfyQuestStatus(quest, false) || !bot->SatisfyQuestExclusiveGroup(quest, false) ||
           !bot->SatisfyQuestClass(quest, false) || !bot->SatisfyQuestRace(quest, false) ||
           !bot->SatisfyQuestLevel(quest, false) || !bot->SatisfyQuestSkill(quest, false) ||
           !bot->SatisfyQuestReputation(quest, false) || !bot->SatisfyQuestTimed(quest, false) ||
           !bot->SatisfyQuestNextChain(quest, false) || !bot->SatisfyQuestBreadcrumb(quest, false) ||
           !bot->SatisfyQuestDay(quest, false) || !bot->SatisfyQuestWeek(quest, false) ||
           !bot->SatisfyQuestMonth(quest, false) || !bot->SatisfyQuestSeasonal(quest, false) ||
           !bot->SatisfyQuestConditions(quest, false);
}

class ChainPlanner
{
public:
    explicit ChainPlanner(Player* bot) : bot(bot) {}

    bool Rewarded(uint32 id) const
    {
        return bot->IsQuestRewarded(id) || std::find(plan.begin(), plan.end(), id) != plan.end();
    }

    // SatisfyQuestPreviousQuest, with the quests already planned counted as rewarded.
    bool PreviousDone(Quest const* quest) const
    {
        if (quest->prevQuests.empty())
            return true;

        for (int32 prev : quest->prevQuests)
        {
            uint32 prevId = std::abs(prev);
            Quest const* prevQuest = sObjectMgr->GetQuestTemplate(prevId);
            if (!prevQuest)
                continue;

            if (prev > 0 && Rewarded(prevId))
            {
                if (prevQuest->GetExclusiveGroup() >= 0)
                    return true;

                bool all = true;
                auto range = sObjectMgr->mExclusiveQuestGroups.equal_range(prevQuest->GetExclusiveGroup());
                for (auto itr = range.first; itr != range.second; ++itr)
                    if (itr->second != prevId && !Rewarded(itr->second))
                        all = false;
                if (all)
                    return true;
            }

            if (prev < 0 && bot->GetQuestStatus(prevId) != QUEST_STATUS_NONE)
                return true;
        }
        return false;
    }

    bool Markable(Quest const* quest) const
    {
        return quest && bot->SatisfyQuestClass(quest, false) && bot->SatisfyQuestRace(quest, false) &&
               !quest->IsRepeatable() && !quest->IsDailyOrWeekly() && !quest->IsMonthly() && !quest->IsSeasonal() &&
               !sDisableMgr->IsDisabledFor(DISABLE_TYPE_QUEST, quest->GetQuestId(), bot);
    }

    bool Add(uint32 id, uint32 depth)
    {
        if (Rewarded(id))
            return true;
        Quest const* quest = sObjectMgr->GetQuestTemplate(id);
        if (!Markable(quest) || !Reach(quest, depth + 1))
            return false;
        if (Rewarded(id))
            return true;
        plan.push_back(id);
        return plan.size() <= MAX_CHAIN;
    }

    // Plans what the quest needs before it: one of its previous quests of the bot's race and class, with its own chain.
    bool Reach(Quest const* quest, uint32 depth)
    {
        if (depth > MAX_DEPTH)
            return false;
        if (PreviousDone(quest))
            return true;

        for (int32 prev : quest->prevQuests)
        {
            if (prev <= 0)
                continue;
            Quest const* prevQuest = sObjectMgr->GetQuestTemplate(prev);
            if (!Markable(prevQuest))
                continue;

            std::vector<uint32> saved = plan;
            std::vector<uint32> members{static_cast<uint32>(prev)};
            if (prevQuest->GetExclusiveGroup() < 0)
            {
                auto range = sObjectMgr->mExclusiveQuestGroups.equal_range(prevQuest->GetExclusiveGroup());
                for (auto itr = range.first; itr != range.second; ++itr)
                    if (itr->second != static_cast<uint32>(prev))
                        members.push_back(itr->second);
            }

            bool ok = true;
            for (uint32 member : members)
                if (!Add(member, depth))
                {
                    ok = false;
                    break;
                }
            if (ok && PreviousDone(quest))
                return true;
            plan = saved;
        }
        return false;
    }

    Player* bot;
    std::vector<uint32> plan;
};
}  // namespace

namespace CoaAltAddon
{
void MarkSeen(Player* master)
{
    if (!master)
        return;
    std::lock_guard<std::mutex> guard(seenLock);
    seenAt[master->GetGUID().GetRawValue()] = time(nullptr);
}

bool MasterHasAddon(Player* master)
{
    if (!master)
        return false;
    std::lock_guard<std::mutex> guard(seenLock);
    auto itr = seenAt.find(master->GetGUID().GetRawValue());
    return itr != seenAt.end() && time(nullptr) - itr->second < SEEN_FOR;
}

bool AddonSeen(PlayerbotAI* botAI)
{
    return botAI && botAI->IsAltBot() && MasterHasAddon(botAI->GetMaster());
}

bool ToAddon(PlayerbotAI* botAI) { return sPlayerbotAIConfig.altChatToAddon && AddonSeen(botAI); }

std::string Field(std::string const& text, size_t max)
{
    std::string out = text;
    std::replace(out.begin(), out.end(), '\t', ' ');
    std::replace(out.begin(), out.end(), '\n', ' ');
    if (out.size() > max)
    {
        while (max > 0 && (static_cast<uint8>(out[max]) & 0xC0) == 0x80)
            --max;
        out.resize(max);
    }
    return out;
}

void Send(Player* bot, Player* to, std::string const& payload)
{
    if (!bot || !to || !bot->IsInWorld())
        return;
    std::string text = "SQBA\t" + payload;
    if (text.size() > 254)
        text.resize(254);
    bot->Whisper(text, LANG_ADDON, to);
}

void SendToMaster(PlayerbotAI* botAI, std::string const& payload)
{
    if (botAI)
        Send(botAI->GetBot(), botAI->GetMaster(), payload);
}

bool OnAddonWhisper(Player* from, Player* receiver, std::string const& msg)
{
    if (!from || !receiver || msg.compare(0, 5, "SQBA\t") != 0)
        return false;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(receiver);
    if (!botAI || botAI->GetMaster() != from || !botAI->IsAltBot())
        return true;

    MarkSeen(from);
    std::string command = msg.substr(5);
    std::replace(command.begin(), command.end(), '\t', ' ');
    if (command.rfind("HELLO", 0) == 0)
    {
        std::ostringstream out;
        out << "HELLO\t" << PROTOCOL << "\t" << (sPlayerbotAIConfig.catchUpQuestChains ? 1 : 0) << "\t"
            << (sPlayerbotAIConfig.altChatToAddon ? 1 : 0);
        Send(receiver, from, out.str());
        return true;
    }

    if (!command.empty())
        botAI->HandleCommand(CHAT_MSG_WHISPER, "alt " + command, from);
    return true;
}

QuestState StateOf(Player* bot, Quest const* quest)
{
    QuestState result;
    uint32 const questId = quest->GetQuestId();
    QuestStatus const status = bot->GetQuestStatus(questId);

    if (InLog(status))
    {
        result.state = "in_log";
        result.detail = status == QUEST_STATUS_COMPLETE ? "complete" : status == QUEST_STATUS_FAILED ? "failed" : "incomplete";
        return result;
    }
    if (bot->CanTakeQuest(quest, false))
    {
        result.state = "can_take";
        return result;
    }
    if (status == QUEST_STATUS_REWARDED || (bot->IsQuestRewarded(questId) && !quest->IsRepeatable()))
    {
        result.state = "done";
        return result;
    }
    if (!bot->SatisfyQuestClass(quest, false) || !bot->SatisfyQuestRace(quest, false))
    {
        result.state = "class_race";
        return result;
    }
    if (!bot->SatisfyQuestLevel(quest, false))
    {
        result.state = "level";
        result.detail = std::to_string(quest->GetMinLevel()) + "-" + std::to_string(quest->GetMaxLevel());
        return result;
    }
    if (!bot->SatisfyQuestSkill(quest, false))
    {
        result.state = "skill";
        result.detail = std::to_string(quest->GetRequiredSkill()) + ":" + std::to_string(quest->GetRequiredSkillValue());
        return result;
    }
    if (!bot->SatisfyQuestReputation(quest, false))
    {
        result.state = "reputation";
        uint32 faction = quest->GetRequiredMinRepFaction() ? quest->GetRequiredMinRepFaction() : quest->GetRequiredMaxRepFaction();
        result.detail = std::to_string(faction);
        return result;
    }
    if (!bot->SatisfyQuestExclusiveGroup(quest, false))
    {
        result.state = "exclusive";
        auto range = sObjectMgr->mExclusiveQuestGroups.equal_range(quest->GetExclusiveGroup());
        for (auto itr = range.first; itr != range.second; ++itr)
            if (itr->second != questId &&
                (bot->GetQuestStatus(itr->second) != QUEST_STATUS_NONE || bot->IsQuestRewarded(itr->second)))
            {
                result.detail = std::to_string(itr->second);
                break;
            }
        return result;
    }
    if (!bot->SatisfyQuestPreviousQuest(quest, false) || !bot->SatisfyQuestPrevChain(quest, false))
    {
        result.state = "missing_prev";
        std::vector<uint32> plan;
        bool const plannable = PlanCatchUp(bot, quest, plan);
        result.catchUp = plannable && sPlayerbotAIConfig.catchUpQuestChains;
        if (plan.empty())
            for (int32 prev : quest->prevQuests)
                if (prev > 0)
                    plan.push_back(prev);
        std::string ids;
        size_t count = 0;
        for (auto itr = plan.rbegin(); itr != plan.rend() && count < MAX_IDS; ++itr, ++count)
            ids += (ids.empty() ? "" : ",") + std::to_string(*itr);
        result.detail = ids;
        return result;
    }
    result.state = "other";
    return result;
}

void SendQuestState(PlayerbotAI* botAI, Quest const* quest)
{
    if (!botAI || !quest)
        return;
    QuestState state = StateOf(botAI->GetBot(), quest);
    std::ostringstream out;
    out << "Q\t" << quest->GetQuestId() << "\t" << state.state << "\t" << state.detail << "\t" << (state.catchUp ? 1 : 0);
    SendToMaster(botAI, out.str());
}

bool PlanCatchUp(Player* bot, Quest const* quest, std::vector<uint32>& plan)
{
    plan.clear();
    if (!bot || !quest || BlockedOutsideChain(bot, quest))
        return false;

    ChainPlanner planner(bot);
    for (uint32 prevChain : quest->prevChainQuests)
        if (InLog(bot->GetQuestStatus(prevChain)))
        {
            Quest const* prevQuest = sObjectMgr->GetQuestTemplate(prevChain);
            if (!planner.Markable(prevQuest) || !planner.Add(prevChain, 0))
                return false;
        }

    if (!planner.Reach(quest, 0))
        return false;

    plan = planner.plan;
    return !plan.empty() && plan.size() <= MAX_CHAIN;
}

uint32 CatchUp(PlayerbotAI* botAI, Quest const* quest)
{
    if (!sPlayerbotAIConfig.catchUpQuestChains || !botAI || !botAI->IsAltBot() || !quest)
        return 0;

    Player* bot = botAI->GetBot();
    std::vector<uint32> plan;
    if (!PlanCatchUp(bot, quest, plan))
        return 0;

    for (uint32 id : plan)
    {
        uint16 slot = bot->FindQuestSlot(id);
        if (slot < MAX_QUEST_LOG_SIZE)
        {
            bot->SetQuestSlot(slot, 0);
            bot->TakeQuestSourceItem(id, false);
        }
        if (bot->GetQuestStatus(id) != QUEST_STATUS_NONE && !bot->IsQuestRewarded(id))
            bot->RemoveActiveQuest(id, false);
        bot->SetRewardedQuest(id);
    }
    bot->SaveToDB(false, false);

    LOG_INFO("playerbots", "{}: chain of quest {} caught up, {} quests marked rewarded", bot->GetName(),
             quest->GetQuestId(), plan.size());

    botAI->TellMasterNoFacing("Chain caught up (" + std::to_string(plan.size()) + " quests): " +
                              ChatHelper::FormatQuest(quest));
    if (AddonSeen(botAI))
        SendToMaster(botAI, "QR\t" + std::to_string(quest->GetQuestId()) + "\tcaught_up\t" + std::to_string(plan.size()));
    return plan.size();
}
}  // namespace CoaAltAddon
