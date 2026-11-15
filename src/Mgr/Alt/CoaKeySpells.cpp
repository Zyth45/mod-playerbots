/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaKeySpells.h"

#include <algorithm>
#include <initializer_list>
#include <vector>

#include "CoaAltAddon.h"
#include "CoaKeySpellsRules.h"
#include "CoaQuickAsk.h"
#include "DBCStores.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

namespace CoaKeySpells
{
namespace
{
// A skill line of the spell is of one of these categories.
bool InSkillCategory(uint32 spellId, std::initializer_list<uint32> categories)
{
    SkillLineAbilityMapBounds const bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
    for (auto it = bounds.first; it != bounds.second; ++it)
        if (SkillLineEntry const* skill = sSkillLineStore.LookupEntry(it->second->SkillLine))
            for (uint32 category : categories)
                if (skill->categoryId == category)
                    return true;
    return false;
}

// The bot knows this spell or another rank of it.
bool KnowsAnyRank(Player* bot, uint32 spellId)
{
    for (uint32 rank = sSpellMgr->GetFirstSpellInChain(spellId); rank; rank = sSpellMgr->GetNextSpellInChain(rank))
        if (bot->HasSpell(rank))
            return true;
    return bot->HasSpell(spellId);
}

void List(PlayerbotAI* botAI)
{
    Player* bot = botAI->GetBot();
    std::vector<uint32> ids;
    // The bot's crowd control is on the list too, whatever its cooldown (one per bot, 3.1.3).
    uint32 const botCc = CoaQuickAsk::BestCc(bot, nullptr, false);
    for (auto const& [spellId, known] : bot->GetSpellMap())
    {
        if (!known || known->State == PLAYERSPELL_REMOVED || !known->Active)
            continue;
        // The highest rank only.
        if (uint32 const next = sSpellMgr->GetNextSpellInChain(spellId); next && bot->HasSpell(next))
            continue;
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
        if (!info)
            continue;
        bool mount = false, createsItem = false;
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            if (info->Effects[i].ApplyAuraName == SPELL_AURA_MOUNTED)
                mount = true;
            if (info->Effects[i].Effect == SPELL_EFFECT_CREATE_ITEM || info->Effects[i].Effect == SPELL_EFFECT_CREATE_ITEM_2)
                createsItem = true;
        }
        uint32 const cdTotal = std::max(info->RecoveryTime, info->CategoryRecoveryTime);
        if (Listed(info->IsPassive(), cdTotal,
                   InSkillCategory(spellId, {SKILL_CATEGORY_PROFESSION, SKILL_CATEGORY_SECONDARY}), mount, createsItem,
                   InSkillCategory(spellId, {SKILL_CATEGORY_GENERIC})) ||
            spellId == botCc)
            ids.push_back(spellId);
    }
    std::sort(ids.begin(), ids.end());
    if (ids.size() > MAX_LINES)
        ids.resize(MAX_LINES);
    for (uint32 spellId : ids)
    {
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
        uint32 const cdTotal = std::max(info->RecoveryTime, info->CategoryRecoveryTime);
        uint32 const cdLeft = bot->HasSpellCooldown(spellId) ? bot->GetSpellCooldownDelay(spellId) : 0;
        CoaAltAddon::SendToMaster(botAI, Line(spellId, cdLeft, cdTotal, Kept(botAI, spellId),
                                              CoaQuickAsk::SpellCategory(info, botCc)));
    }
    CoaAltAddon::SendToMaster(botAI, "KSPELL\t-");
}

void Keep(PlayerbotAI* botAI, std::string const& arg)
{
    uint32_t spellId = 0;
    bool on = false;
    if (!ParseKeep(arg, spellId, on) || !sSpellMgr->GetSpellInfo(spellId) || !KnowsAnyRank(botAI->GetBot(), spellId))
    {
        std::string const id = arg.substr(0, arg.find(' '));
        CoaAltAddon::SendToMaster(botAI, "KCD\terr\t" + (id.empty() ? std::string("0") : id) + "\tunknown");
        return;
    }
    uint32 const first = sSpellMgr->GetFirstSpellInChain(spellId);
    if (on)
        botAI->coaKeptSpells.insert(first ? first : spellId);
    else
        botAI->coaKeptSpells.erase(first ? first : spellId);
    CoaAltAddon::SendToMaster(botAI, "KCD\tdone\t" + std::to_string(spellId) + "\t" + (on ? "1" : "0"));
}
}  // namespace

bool Command(PlayerbotAI* botAI, std::string const& command, std::string const& arg)
{
    if (command != "keyspells" && command != "keepcd")
        return false;
    if (!botAI || !botAI->GetBot())
        return true;
    if (!sPlayerbotAIConfig.coaKeySpells)
    {
        if (command == "keyspells")
            CoaAltAddon::SendToMaster(botAI, "KSPELL\terr\toff");
        else
        {
            std::string const id = arg.substr(0, arg.find(' '));
            CoaAltAddon::SendToMaster(botAI, "KCD\terr\t" + (id.empty() ? std::string("0") : id) + "\toff");
        }
        return true;
    }
    if (command == "keyspells")
        List(botAI);
    else
        Keep(botAI, arg);
    return true;
}

bool Kept(PlayerbotAI* botAI, uint32_t spellId)
{
    if (!botAI || botAI->coaKeptSpells.empty())
        return false;
    uint32 const first = sSpellMgr->GetFirstSpellInChain(spellId);
    return botAI->coaKeptSpells.count(first ? first : spellId) > 0;
}
}  // namespace CoaKeySpells
