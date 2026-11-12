/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaAltAddon.h"

#include "Bag.h"
#include "ChatHelper.h"
#include "CoaEnchanter.h"
#include "DisableMgr.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "QuestDef.h"
#include "ScriptMgr.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Trainer.h"
#include "World.h"

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
struct Seen
{
    time_t at = 0;
    uint32 hello = 0;
};
std::unordered_map<uint64, Seen> seenAt;

// The HELLO count of the owner: a new HELLO (login, /reload) sends the team bar and the objectives again.
uint32 HelloCount(Player* master)
{
    std::lock_guard<std::mutex> guard(seenLock);
    auto itr = seenAt.find(master->GetGUID().GetRawValue());
    return itr == seenAt.end() ? 0 : itr->second.hello;
}

// The team dashboard: how often STAT and PROF are looked at (and so sent at most).
constexpr time_t STAT_EVERY = 30;
constexpr time_t PROF_EVERY = 10;
constexpr size_t PROF_ROOM = 240;

// Primary professions first, then the secondary ones.
uint32 const SHEET_SKILLS[] = { SKILL_ALCHEMY,   SKILL_BLACKSMITHING, SKILL_ENCHANTING,     SKILL_ENGINEERING,
                                SKILL_HERBALISM, SKILL_INSCRIPTION,   SKILL_JEWELCRAFTING,  SKILL_LEATHERWORKING,
                                SKILL_MINING,    SKILL_SKINNING,      SKILL_TAILORING,      SKILL_COOKING,
                                SKILL_FIRST_AID, SKILL_FISHING };

// The level the trainers of this server ask for each profession rank spell (Journeyman Blacksmith...): the lowest
// one, whether the trainer row is the rank spell or a spell that teaches it. A rank no trainer teaches is not here.
// ponytail: built once at first use (thread-safe static), a `.reload` of the trainers does not rebuild it
std::unordered_map<uint32, uint8> const& RankLevels()
{
    static std::unordered_map<uint32, uint8> const levels = []
    {
        std::unordered_map<uint32, uint8> result;
        auto note = [&result](uint32 spellId, uint8 level)
        {
            SpellLearnSkillNode const* node = sSpellMgr->GetSpellLearnSkill(spellId);
            if (!node || !IsProfessionSkill(node->skill))
                return;
            auto [itr, inserted] = result.try_emplace(spellId, level);
            if (!inserted)
                itr->second = std::min(itr->second, level);
        };
        for (auto const& [trainerId, trainer] : sObjectMgr->GetTrainers())
            for (Trainer::Spell const& spell : trainer.GetSpells())
            {
                note(spell.SpellId, spell.ReqLevel);
                if (SpellInfo const* info = sSpellMgr->GetSpellInfo(spell.SpellId))
                    for (SpellEffectInfo const& effect : info->GetEffects())
                        if (effect.IsEffect(SPELL_EFFECT_LEARN_SPELL) && effect.TriggerSpell)
                            note(effect.TriggerSpell, spell.ReqLevel);
            }
        return result;
    }();
    return levels;
}

// "<skill>:<value>:<max>:<step>:<next step>:<level of the next step>", next step 0 when no trainer teaches more.
std::string ProfEntry(Player* bot, uint32 skill)
{
    uint16 const step = bot->GetSkillStep(skill);
    uint32 nextStep = 0;
    uint32 nextLevel = 0;
    for (uint32 rankSpell : sSpellMgr->GetSkillRankSpells(skill))
    {
        SpellLearnSkillNode const* node = sSpellMgr->GetSpellLearnSkill(rankSpell);
        if (!node || node->step != step + 1)
            continue;
        auto itr = RankLevels().find(rankSpell);
        if (itr == RankLevels().end())
            continue;
        uint32 level = itr->second;
        if (!level)
            if (SpellInfo const* info = sSpellMgr->GetSpellInfo(rankSpell))
                level = std::max(info->BaseLevel, info->SpellLevel);
        if (!nextStep || level < nextLevel)
            nextLevel = level;
        nextStep = node->step;
    }
    std::ostringstream out;
    out << skill << ":" << bot->GetPureSkillValue(skill) << ":" << bot->GetPureMaxSkillValue(skill) << ":" << step << ":"
        << nextStep << ":" << nextLevel;
    return out.str();
}

std::string ProfLine(Player* bot)
{
    std::string entries;
    for (uint32 skill : SHEET_SKILLS)
    {
        if (!bot->HasSkill(skill))
            continue;
        std::string const entry = ProfEntry(bot, skill);
        if (entries.size() + entry.size() + 1 > PROF_ROOM)
            break;
        entries += (entries.empty() ? "" : ",") + entry;
    }
    return "PROF\t" + std::to_string(bot->GetFreePrimaryProfessionPoints()) + "\t" + entries;
}

std::string StatLine(Player* bot)
{
    uint32 lowest = 100;
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            if (uint32 const max = item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY))
                lowest = std::min<uint32>(lowest, item->GetUInt32Value(ITEM_FIELD_DURABILITY) * 100 / max);

    // As the "stats" command counts them: the backpack and the plain bags (no quiver, no profession bag).
    uint32 slots = INVENTORY_SLOT_ITEM_END - INVENTORY_SLOT_ITEM_START;
    uint32 freeSlots = 0;
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (!bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            ++freeSlots;
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
        if (Bag const* pBag = bot->GetBagByPos(bag))
        {
            ItemTemplate const* proto = pBag->GetTemplate();
            if (proto->Class == ITEM_CLASS_CONTAINER && proto->SubClass == ITEM_SUBCLASS_CONTAINER)
            {
                slots += pBag->GetBagSize();
                freeSlots += pBag->GetFreeSlots();
            }
        }

    bool const maxLevel = bot->GetLevel() >= sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
    std::ostringstream out;
    out << "STAT\t" << uint32(bot->GetLevel()) << "\t" << (maxLevel ? 0 : bot->GetUInt32Value(PLAYER_XP)) << "\t"
        << (maxLevel ? 0 : bot->GetUInt32Value(PLAYER_NEXT_LEVEL_XP)) << "\t" << bot->GetMoney() << "\t" << freeSlots << "\t"
        << slots << "\t" << lowest << "\t" << uint32(bot->getClass()) << "\t"
        << (maxLevel ? 0 : bot->GetUInt32Value(PLAYER_REST_STATE_EXPERIENCE));
    return out.str();
}

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
    seenAt[master->GetGUID().GetRawValue()].at = time(nullptr);
}

bool MasterHasAddon(Player* master)
{
    if (!master)
        return false;
    std::lock_guard<std::mutex> guard(seenLock);
    auto itr = seenAt.find(master->GetGUID().GetRawValue());
    return itr != seenAt.end() && time(nullptr) - itr->second.at < SEEN_FOR;
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
    if (!botAI || botAI->GetMaster() != from)
        return true;

    std::string command = msg.substr(5);
    std::replace(command.begin(), command.end(), '\t', ' ');
    // The Main Tank bar talks to the bot tanks of the group, alts or random bots alike (contract of 04/11).
    if (command.rfind("tank ", 0) == 0)
    {
        MarkSeen(from);
        botAI->HandleCommand(CHAT_MSG_WHISPER, command, from);
        return true;
    }
    if (!botAI->IsAltBot())
    {
        // A player with no alt (a raid of random bots) announces the addon to any bot of the group.
        if (command.rfind("HELLO", 0) == 0)
            MarkSeen(from);
        return true;
    }

    MarkSeen(from);
    if (command.rfind("HELLO", 0) == 0)
    {
        {
            std::lock_guard<std::mutex> guard(seenLock);
            ++seenAt[from->GetGUID().GetRawValue()].hello;
        }
        std::ostringstream out;
        out << "HELLO\t" << PROTOCOL << "\t" << (sPlayerbotAIConfig.catchUpQuestChains ? 1 : 0) << "\t"
            << (sPlayerbotAIConfig.altChatToAddon ? 1 : 0) << "\t1\t" << (sPlayerbotAIConfig.coaAltBags ? 1 : 0);
        Send(receiver, from, out.str());
        CoaEnchanter::SendStatus(botAI);
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

void TickTeam(PlayerbotAI* botAI, TeamCache& cache)
{
    if (!botAI || !botAI->IsAltBot())
        return;
    Player* bot = botAI->GetBot();
    Player* master = botAI->GetMaster();
    if (!bot || !master || !master->IsInWorld() || !MasterHasAddon(master))
        return;

    uint32 const hello = HelloCount(master);
    if (hello != cache.hello)
    {
        cache = TeamCache();
        cache.hello = hello;
    }

    char const* state = "follow";
    if (!bot->IsAlive())
        state = "dead";
    else if (botAI->HasStrategy("mission", BOT_STATE_NON_COMBAT))
        state = "mission";
    else if (bot->GetMapId() != master->GetMapId() || bot->GetDistance(master) > 100.0f)
        state = "far";
    else if (botAI->HasStrategy("stay", BOT_STATE_NON_COMBAT))
        state = "stay";

    uint32 const hp = static_cast<uint32>(bot->GetHealthPct() + 2.5f) / 5 * 5;
    std::ostringstream team;
    team << "TEAM\t" << bot->GetLevel() << "\t" << hp << "\t" << state << "\t" << bot->GetFreeInventorySpace() << "\t"
         << bot->GetZoneId();
    time_t const now = time(nullptr);
    if (team.str() != cache.team || now - cache.teamAt >= MINUTE)
    {
        cache.team = team.str();
        cache.teamAt = now;
        Send(bot, master, cache.team);
    }

    std::map<uint32, std::string> seen;
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 const questId = master->GetQuestSlotQuestId(slot);
        Quest const* quest = questId ? sObjectMgr->GetQuestTemplate(questId) : nullptr;
        if (!quest)
            continue;

        std::ostringstream out;
        QuestStatus const status = bot->GetQuestStatus(questId);
        out << "OBJ\t" << questId << "\t";
        if (status == QUEST_STATUS_REWARDED || bot->IsQuestRewarded(questId))
            out << "done\t";
        else if (!InLog(status))
            out << "none\t";
        else
        {
            out << (status == QUEST_STATUS_COMPLETE ? "complete" : status == QUEST_STATUS_FAILED ? "failed" : "incomplete")
                << "\t";
            auto itr = bot->getQuestStatusMap().find(questId);
            bool first = true;
            if (itr != bot->getQuestStatusMap().end())
            {
                for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
                    if (quest->RequiredNpcOrGoCount[i])
                    {
                        out << (first ? "" : ",") << std::min<uint32>(itr->second.CreatureOrGOCount[i], quest->RequiredNpcOrGoCount[i])
                            << "/" << quest->RequiredNpcOrGoCount[i];
                        first = false;
                    }
                for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
                    if (quest->RequiredItemCount[i])
                    {
                        out << (first ? "" : ",") << std::min<uint32>(itr->second.ItemCount[i], quest->RequiredItemCount[i])
                            << "/" << quest->RequiredItemCount[i];
                        first = false;
                    }
            }
        }

        std::string const line = out.str();
        seen[questId] = line;
        auto sent = cache.objectives.find(questId);
        if (sent == cache.objectives.end() || sent->second != line)
            Send(bot, master, line);
    }
    cache.objectives.swap(seen);

    SendSheet(botAI, cache, false);
}

void SendSheet(PlayerbotAI* botAI, TeamCache& cache, bool force)
{
    if (!botAI || !botAI->IsAltBot())
        return;
    Player* bot = botAI->GetBot();
    Player* master = botAI->GetMaster();
    if (!bot || !master || !master->IsInWorld())
        return;

    time_t const now = time(nullptr);
    if (force || now - cache.statAt >= STAT_EVERY)
    {
        cache.statAt = now;
        std::string const line = StatLine(bot);
        if (force || line != cache.stat)
        {
            cache.stat = line;
            Send(bot, master, line);
        }
    }
    if (force || now - cache.profAt >= PROF_EVERY)
    {
        cache.profAt = now;
        std::string const line = ProfLine(bot);
        if (force || line != cache.prof)
        {
            cache.prof = line;
            Send(bot, master, line);
        }
    }
}
}  // namespace CoaAltAddon
