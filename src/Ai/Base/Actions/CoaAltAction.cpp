/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaAltAction.h"

#include "ChatHelper.h"
#include "CoaAltAddon.h"
#include "CoaEnchanter.h"
#include "Event.h"
#include "GameObject.h"
#include "ObjectAccessor.h"
#include "Playerbots.h"
#include "TrainerAction.h"

namespace
{
std::string Readable(CoaAltAddon::QuestState const& state)
{
    if (state.state == "can_take")
        return "I can take it";
    if (state.state == "in_log")
        return "already in my log";
    if (state.state == "done")
        return "already done";
    if (state.state == "missing_prev")
        return std::string("earlier quests missing (") + state.detail + ")" +
               (state.catchUp ? ", whisper \"alt catchup\" to catch up" : "");
    if (state.state == "exclusive")
        return "I chose another quest of this choice (" + state.detail + ")";
    if (state.state == "level")
        return "level " + state.detail;
    if (state.state == "class_race")
        return "wrong class or race";
    if (state.state == "reputation")
        return "reputation too low";
    if (state.state == "skill")
        return "profession skill too low";
    return "not available to me";
}

// A quest link, or a plain quest id.
Quest const* QuestOf(Player* master, std::string const& arg)
{
    PlayerbotChatHandler handler(master);
    uint32 questId = handler.extractQuestId(arg);
    if (!questId && !arg.empty() && arg.find_first_not_of("0123456789") == std::string::npos && arg.size() < 10)
        questId = uint32(std::stoul(arg));
    return questId ? sObjectMgr->GetQuestTemplate(questId) : nullptr;
}
}  // namespace

bool CoaQuestTakeAction::Execute(Event event)
{
    Player* master = GetMaster();
    Player* requester = event.getOwner() ? event.getOwner() : master;
    if (!master || requester != master || !botAI->IsAltBot())
        return false;

    std::string arg = event.getParam();
    arg.erase(0, arg.find_first_not_of(' '));
    arg.erase(arg.find_last_not_of(' ') + 1);
    Quest const* quest = QuestOf(master, arg);
    if (!quest)
    {
        botAI->TellMaster("Usage: quest take <quest id or link>");
        return false;
    }
    return Take(quest, false);
}

bool CoaAltAction::Execute(Event event)
{
    Player* master = GetMaster();
    Player* requester = event.getOwner() ? event.getOwner() : master;
    if (!master || requester != master || !botAI->IsAltBot())
        return false;

    std::string const param = event.getParam();
    std::string command = param;
    std::string arg;
    size_t const space = param.find(' ');
    if (space != std::string::npos)
    {
        command = param.substr(0, space);
        arg = param.substr(space + 1);
        arg.erase(0, arg.find_first_not_of(' '));
    }

    if (command == "hello")
    {
        CoaAltAddon::MarkSeen(master);
        std::ostringstream out;
        out << "HELLO\t" << CoaAltAddon::PROTOCOL << "\t" << (sPlayerbotAIConfig.catchUpQuestChains ? 1 : 0) << "\t"
            << (sPlayerbotAIConfig.altChatToAddon ? 1 : 0) << "\t1";
        CoaAltAddon::SendToMaster(botAI, out.str());
        CoaEnchanter::SendStatus(botAI);
        return true;
    }

    // "alt sheet": the team dashboard of the addon asks for STAT and PROF now.
    if (command == "sheet")
    {
        if (CoaAltAddon::AddonSeen(botAI))
            CoaAltAddon::SendSheet(botAI, botAI->coaTeamCache, true);
        return true;
    }

    // "alt enchanter [set <name>|clear|mode mail|mode bank]": the enchanter of the team (CoaEnchanter).
    if (command == "enchanter")
    {
        botAI->TellMasterNoFacing(CoaEnchanter::Command(botAI, master, arg));
        return true;
    }

    if (command == "train" || command == "learn")
    {
        bool const learn = command == "learn";
        uint32 spellId = 0;
        if (learn && arg != "all")
        {
            PlayerbotChatHandler handler(master);
            spellId = handler.extractSpellId(arg);
            if (!spellId)
            {
                botAI->TellMaster("Usage: alt learn all | alt learn <spell>");
                return false;
            }
        }
        TrainerAction trainer(botAI);
        return trainer.RunForAddon(learn, spellId);
    }

    if (command != "quest" && command != "take" && command != "catchup")
    {
        botAI->TellMaster("Usage: alt quest|take|catchup <quest>, alt train, alt learn all|<spell>, "
                          "alt enchanter [set <name>|clear|mode mail|bank]");
        return false;
    }

    Quest const* quest = QuestOf(master, arg);
    if (!quest)
    {
        botAI->TellMaster("Unknown quest.");
        return false;
    }

    if (command == "quest")
    {
        if (CoaAltAddon::AddonSeen(botAI))
            CoaAltAddon::SendQuestState(botAI, quest);
        else
            botAI->TellMaster(chat->FormatQuest(quest) + ": " + Readable(CoaAltAddon::StateOf(bot, quest)));
        return true;
    }

    return Take(quest, command == "catchup");
}

void CoaAltAction::Refused(Quest const* quest, std::string const& state)
{
    if (CoaAltAddon::AddonSeen(botAI))
    {
        CoaAltAddon::SendToMaster(botAI, "QR\t" + std::to_string(quest->GetQuestId()) + "\trefused\t" + state);
        CoaAltAddon::SendQuestState(botAI, quest);
        return;
    }
    botAI->TellMaster("Can't take " + chat->FormatQuest(quest) + ": " + Readable(CoaAltAddon::StateOf(bot, quest)));
}

bool CoaAltAction::Take(Quest const* quest, bool catchUp)
{
    uint32 const questId = quest->GetQuestId();

    if (!bot->CanTakeQuest(quest, false))
    {
        CoaAltAddon::QuestState state = CoaAltAddon::StateOf(bot, quest);
        if (state.state != "missing_prev" || !CoaAltAddon::CatchUp(botAI, quest))
        {
            Refused(quest, state.state);
            return false;
        }
    }
    else if (catchUp && CoaAltAddon::AddonSeen(botAI))
        CoaAltAddon::SendToMaster(botAI, "QR\t" + std::to_string(questId) + "\tcaught_up\t0");

    if (!bot->CanTakeQuest(quest, false))
    {
        Refused(quest, CoaAltAddon::StateOf(bot, quest).state);
        return false;
    }

    Player* master = GetMaster();
    if (!master)
        return false;

    ObjectGuid giver;
    if (master->GetMap() == bot->GetMap())
        if (Unit* target = botAI->GetUnit(master->GetTarget()))
            if (target->hasQuest(questId))
                giver = target->GetGUID();

    if (!giver)
        for (ObjectGuid const& guid : AI_VALUE(GuidVector, "nearest npcs"))
            if (Unit* unit = botAI->GetUnit(guid))
                if (unit->hasQuest(questId))
                {
                    giver = guid;
                    break;
                }

    if (!giver)
        for (ObjectGuid const& guid : AI_VALUE(GuidVector, "nearest game objects no los"))
            if (GameObject* go = botAI->GetGameObject(guid))
                if (go->hasQuest(questId))
                {
                    giver = guid;
                    break;
                }

    QuestStatus const masterStatus = master->GetQuestStatus(questId);
    bool const masterHasIt = masterStatus == QUEST_STATUS_INCOMPLETE || masterStatus == QUEST_STATUS_COMPLETE;

    // The core takes a quest only at the quest giver: one out of reach is used when the player does not have the
    // quest (else the alt takes it from the player's log, below), and only within AltLootMasterDistance of the player.
    if (giver)
    {
        WorldObject* object = ObjectAccessor::GetWorldObject(*bot, giver);
        if (object && bot->IsWithinDistInMap(object, INTERACTION_DISTANCE))
            return AcceptQuest(quest, giver);
        if (!masterHasIt)
        {
            bool const nearMaster = object && master->IsWithinDistInMap(object, sPlayerbotAIConfig.altLootMasterDistance);
            if (CoaAltAddon::AddonSeen(botAI))
                CoaAltAddon::SendToMaster(botAI, "QR	" + std::to_string(questId) + (nearMaster ? "	too_far	" : "	no_giver	"));
            else
                botAI->TellMaster(std::string(nearMaster ? "Bring me to the quest giver of " : "No quest giver near me for ") +
                                  chat->FormatQuest(quest));
            return false;
        }
    }

    if (!masterHasIt)
    {
        if (CoaAltAddon::AddonSeen(botAI))
            CoaAltAddon::SendToMaster(botAI, "QR\t" + std::to_string(questId) + "\tno_giver\t");
        else
            botAI->TellMaster("No quest giver near me for " + chat->FormatQuest(quest));
        return false;
    }

    if (!bot->SatisfyQuestLog(false) || !bot->CanAddQuest(quest, false))
    {
        bool const logFull = !bot->SatisfyQuestLog(false);
        if (CoaAltAddon::AddonSeen(botAI))
            CoaAltAddon::SendToMaster(botAI, "QR\t" + std::to_string(questId) + (logFull ? "\tlog_full\t" : "\tbags_full\t"));
        botAI->TellMaster(std::string(logFull ? "Quest log is full " : "Bags are full ") + chat->FormatQuest(quest));
        return false;
    }

    bot->AddQuest(quest, master);
    if (bot->CanCompleteQuest(questId))
        bot->CompleteQuest(questId);

    if (CoaAltAddon::AddonSeen(botAI))
        CoaAltAddon::SendToMaster(botAI, "QR\t" + std::to_string(questId) + "\ttaken\t");
    else
        botAI->TellMaster("Accepted " + chat->FormatQuest(quest));
    return true;
}
