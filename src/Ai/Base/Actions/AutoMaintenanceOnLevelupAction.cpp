/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "AutoMaintenanceOnLevelupAction.h"
#include "CoaSpecialization.h"
#include "CoaSpecialization.h"
#include "CoaWildcard.h"
#include "CoaReborn.h"
#include "BroadcastHelper.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "RandomPlayerbotMgr.h"
#include "SharedDefines.h"
#include "SpellMgr.h"
#include "GameTime.h"

#include <mutex>
#include <unordered_map>

namespace
{
// AiPlayerbot.CoaLive.MaintenanceSpread: the step a bot does next and when (ms). Each level-up starts again at 1.
std::mutex maintenanceLock;
std::unordered_map<uint64, std::pair<uint32, uint64>> maintenancePending;
constexpr uint32 MAINTENANCE_LAST_STEP = 4;
}  // namespace

bool AutoMaintenanceOnLevelupAction::Execute(Event /*event*/)
{
    // A CoA bot reaching level 10 picks its specialization: switch its combat role now
    // rather than at the next strategy reset.
    if (EnsureCoaSpecialization(bot) || DraftWildcardBuild(bot))
    {
        if (GetCoaRole(bot) == CoaRole::Tank)
        {
            botAI->ChangeStrategy("-coa,-coa ranged,-dps assist,+coa tank,+tank assist,+tank face", BOT_STATE_COMBAT);
            botAI->ChangeStrategy("-dps assist,+tank assist", BOT_STATE_NON_COMBAT);
        }
        else if (GetCoaRole(bot) == CoaRole::Heal)
            botAI->ChangeStrategy("-coa,-coa ranged,+coa heal", BOT_STATE_COMBAT);
        // The specialization may fight at another range than the class default used until now.
        else if (GetCoaStyle(bot) == CoaStyle::Melee)
            botAI->ChangeStrategy("-coa ranged,+coa", BOT_STATE_COMBAT);
        else
            botAI->ChangeStrategy("-coa,+coa ranged", BOT_STATE_COMBAT);
    }

    // One Character Advancement point per level from 10. Capped as the rebuild is (TalentsPerUpdate): left
    // uncapped, this action took over the whole cost the cap had spread (20 -> 105 ms a call, a15 bench).
    ApplyCoaTalents(bot, CoaTalentsPerUpdate());
    SpendAltBotTalentPoints(bot);
    // A random Warcraft Reborn bot takes the talents of its tree's build due at its new level.
    if (IsRebornBot(bot) && sRandomPlayerbotMgr.IsRandomBot(bot))
        ApplyWcrBuild(bot);

    AutoPickTalents();

    // The rest of the maintenance cost 55 to 69 ms in one go: spread, it runs a step a second on the bot's
    // next updates, in the same order.
    if (sPlayerbotAIConfig.coaLiveMaintenanceSpread)
    {
        std::lock_guard<std::mutex> guard(maintenanceLock);
        if (maintenancePending.size() > 20000)
            maintenancePending.clear();
        maintenancePending[bot->GetGUID().GetRawValue()] = {1, GameTime::GetGameTimeMS().count() + 1000};
        return true;
    }
    for (uint32 step = 1; step <= MAINTENANCE_LAST_STEP; ++step)
        RunStep(step);

    return true;
}

void AutoMaintenanceOnLevelupAction::RunStep(uint32 step)
{
    switch (step)
    {
        case 1: AutoLearnSpell(); break;
        case 2: AutoTeleportForLevel(); break;
        case 3: AutoUpgradeEquipConsumables(); break;
        case 4: AutoUpgradeEquipGear(); break;
        default: break;
    }
}

void AutoMaintenanceOnLevelupAction::Tick(PlayerbotAI* botAI)
{
    Player* bot = botAI ? botAI->GetBot() : nullptr;
    if (!bot || !bot->IsInWorld())
        return;
    uint64 const key = bot->GetGUID().GetRawValue();
    uint64 const now = GameTime::GetGameTimeMS().count();
    uint32 step = 0;
    {
        std::lock_guard<std::mutex> guard(maintenanceLock);
        auto it = maintenancePending.find(key);
        if (it == maintenancePending.end() || now < it->second.second)
            return;
        if (bot->IsInCombat())
        {
            it->second.second = now + 1000;  // after the fight, as the whole maintenance waited for it before
            return;
        }
        step = it->second.first;
        if (step >= MAINTENANCE_LAST_STEP)
            maintenancePending.erase(it);
        else
            it->second = {step + 1, now + 1000};
    }
    AutoMaintenanceOnLevelupAction action(botAI);
    action.RunStep(step);
}

void AutoMaintenanceOnLevelupAction::AutoTeleportForLevel()
{
    if (!sPlayerbotAIConfig.autoTeleportForLevel || !sRandomPlayerbotMgr.IsRandomBot(bot))
        return;

    if (botAI->HasGameClientMaster())
        return;

    // CoA: a bot re-rolled to the level of the player it was just offered to (lfg bot) stays where it is: the far
    // teleport resets its AI, which dropped the queued "accept invitation", and every later invite said "already
    // in a group" (12/10). Accepting brings it to the player anyway.
    if (CoaLfgOfferedToOther(bot, nullptr) || bot->GetGroupInvite())
        return;

    sRandomPlayerbotMgr.RandomTeleportForLevel(bot);
    return;
}

void AutoMaintenanceOnLevelupAction::AutoPickTalents()
{
    if (!sPlayerbotAIConfig.autoPickTalents || !sRandomPlayerbotMgr.IsRandomBot(bot))
        return;

    if (bot->GetFreeTalentPoints() <= 0)
        return;

    PlayerbotFactory factory(bot, bot->GetLevel());
    factory.InitTalentsTree(true, true, true);
    factory.InitPetTalents();
}

void AutoMaintenanceOnLevelupAction::AutoLearnSpell()
{
    std::ostringstream out;
    LearnSpells(&out);

    if (!out.str().empty())
    {
        std::string const temp = out.str();
        out.seekp(0);
        out << "Learned spells: ";
        out << temp;
        out.seekp(-2, out.cur);
        out << ".";
        botAI->TellMaster(out);
    }
    return;
}

void AutoMaintenanceOnLevelupAction::LearnSpells(std::ostringstream* out)
{
    BroadcastHelper::BroadcastLevelup(botAI, bot);
    if (sPlayerbotAIConfig.autoLearnTrainerSpells && sRandomPlayerbotMgr.IsRandomBot(bot))
        LearnTrainerSpells(out);

    if (sPlayerbotAIConfig.autoLearnQuestSpells && sRandomPlayerbotMgr.IsRandomBot(bot))
        LearnQuestSpells(out);
}

void AutoMaintenanceOnLevelupAction::LearnTrainerSpells(std::ostringstream* /*out*/)
{
    PlayerbotFactory factory(bot, bot->GetLevel());
    factory.InitSkills();
    factory.InitClassSpells();
    factory.InitAvailableSpells();
    factory.InitPet();
}

void AutoMaintenanceOnLevelupAction::LearnQuestSpells(std::ostringstream* out)
{
    ObjectMgr::QuestMap const& questTemplates = sObjectMgr->GetQuestTemplates();
    for (ObjectMgr::QuestMap::const_iterator i = questTemplates.begin(); i != questTemplates.end(); ++i)
    {
        Quest const* quest = i->second;

        if (!quest->GetRequiredClasses() || quest->IsRepeatable() || quest->GetMinLevel() < 10 ||
            quest->GetMinLevel() > bot->GetLevel())
        {
            continue;
        }

        if (!bot->SatisfyQuestClass(quest, false) || !bot->SatisfyQuestRace(quest, false) ||
            !bot->SatisfyQuestSkill(quest, false))
        {
            continue;
        }

        int32 spellId = quest->GetRewSpellCast();
        if (!spellId)
            continue;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo)
            continue;

        bool found = false;
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            if (spellInfo->Effects[i].Effect == SPELL_EFFECT_LEARN_SPELL && spellInfo->Effects[i].TriggerSpell &&
                !bot->HasSpell(spellInfo->Effects[i].TriggerSpell))
            {
                if (PlayerbotFactory::IsCoaForeignClassSpell(bot, spellInfo->Effects[i].TriggerSpell))
                    break;

                if (SpellInfo const* triggeredInfo = sSpellMgr->GetSpellInfo(spellInfo->Effects[i].TriggerSpell))
                    if (triggeredInfo->Effects[0].Effect == SPELL_EFFECT_TRADE_SKILL)
                        break;

                found = true;
                break;
            }
        }

        if (!found)
            continue;

        bot->CastSpell(bot, spellId, true);

        uint32 rewSpellId = quest->GetRewSpell();
        if (rewSpellId)
        {
            if (SpellInfo const* rewSpellInfo = sSpellMgr->GetSpellInfo(rewSpellId))
            {
                *out << FormatSpell(rewSpellInfo) << ", ";
                continue;
            }
        }

        *out << FormatSpell(spellInfo) << ", ";
    }
}

std::string const AutoMaintenanceOnLevelupAction::FormatSpell(SpellInfo const* sInfo)
{
    std::ostringstream out;
    std::string const rank = sInfo->Rank[0];

    if (rank.empty())
        out << "|cffffffff|Hspell:" << sInfo->Id << "|h[" << sInfo->SpellName[LOCALE_enUS] << "]|h|r";
    else
        out << "|cffffffff|Hspell:" << sInfo->Id << "|h[" << sInfo->SpellName[LOCALE_enUS] << " " << rank << "]|h|r";

    return out.str();
}

void AutoMaintenanceOnLevelupAction::AutoUpgradeEquip()
{
    AutoUpgradeEquipConsumables();
    AutoUpgradeEquipGear();
}

void AutoMaintenanceOnLevelupAction::AutoUpgradeEquipConsumables()
{
    if (!sRandomPlayerbotMgr.IsRandomBot(bot))
        return;
    // The same factory as the gear's (autogear limits for a player's bot), as when both were one step.
    bool const playersBot = botAI->HasGameClientMaster();
    uint32 const quality = playersBot ? static_cast<uint32>(sPlayerbotAIConfig.autoGearQualityLimit) : 0;
    uint32 const gearScore = playersBot && sPlayerbotAIConfig.autoGearScoreLimit
                                 ? PlayerbotFactory::CalcMixedGearScore(sPlayerbotAIConfig.autoGearScoreLimit,
                                                                        sPlayerbotAIConfig.autoGearQualityLimit)
                                 : 0;
    PlayerbotFactory factory(bot, bot->GetLevel(), quality, gearScore);
    factory.CleanupConsumables();
    factory.InitAmmo();
    factory.InitReagents();
    factory.InitFood();
    factory.InitConsumables();
    factory.InitPotions();
}

void AutoMaintenanceOnLevelupAction::AutoUpgradeEquipGear()
{
    if (!sRandomPlayerbotMgr.IsRandomBot(bot))
        return;

    // A bot in a player's group is one of the player's bots: on level up it follows the autogear settings (can it gear
    // itself, best quality) like the autogear command, not the gear of the random bots.
    bool const playersBot = botAI->HasGameClientMaster();
    uint32 const quality = playersBot ? static_cast<uint32>(sPlayerbotAIConfig.autoGearQualityLimit) : 0;
    uint32 const gearScore = playersBot && sPlayerbotAIConfig.autoGearScoreLimit
                                 ? PlayerbotFactory::CalcMixedGearScore(sPlayerbotAIConfig.autoGearScoreLimit,
                                                                        sPlayerbotAIConfig.autoGearQualityLimit)
                                 : 0;
    PlayerbotFactory factory(bot, bot->GetLevel(), quality, gearScore);

    if (sPlayerbotAIConfig.autoUpgradeEquip && PlayerbotFactory::MayGearItself(bot))
        factory.InitEquipment(true);
}
