/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAFORMULAS_H
#define PLAYERBOTS_COAFORMULAS_H

#include "CoaFormulaRules.h"
#include "ItemTemplate.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

// CoA 3.3.1 ("super enchanters"): the one shared rule for the recipes a bot may learn. Header only, so the loot roll
// and the item usage (CoA SquidBot) read the same rule as the economy.
namespace CoaEconomy::Formulas
{
// The spell a recipe item teaches (both CoA formats, CoaFormulaRules::TaughtSpell); 0 when it is no recipe.
inline uint32 TaughtSpell(ItemTemplate const* proto)
{
    if (!proto || proto->Class != ITEM_CLASS_RECIPE)
        return 0;
    uint32 ids[MAX_ITEM_PROTO_SPELLS];
    for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
        ids[i] = proto->Spells[i].SpellId > 0 ? uint32(proto->Spells[i].SpellId) : 0;
    return CoaFormulaRules::TaughtSpell(ids, MAX_ITEM_PROTO_SPELLS, [](uint32 id) -> uint32
    {
        SpellInfo const* spell = sSpellMgr->GetSpellInfo(id);
        if (!spell)
            return 0;
        for (SpellEffectInfo const& effect : spell->Effects)
            if (effect.Effect == SPELL_EFFECT_LEARN_SPELL && effect.TriggerSpell)
                return effect.TriggerSpell;
        return 0;
    });
}

// The profession skill line of a taught spell, 0 when it is none.
inline uint32 ProfessionOf(uint32 spellId)
{
    SkillLineAbilityMapBounds const bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
    for (auto it = bounds.first; it != bounds.second; ++it)
    {
        SkillLineEntry const* skill = sSkillLineStore.LookupEntry(it->second->SkillLine);
        if (skill && (skill->categoryId == SKILL_CATEGORY_PROFESSION || skill->categoryId == SKILL_CATEGORY_SECONDARY))
            return skill->id;
    }
    return 0;
}

// 3.3.1: a recipe item the bot won at a need roll (CoA SquidBot's roll calls it when the bot gets the item): when the
// bot learns it from its bags, the dashboard's feed says "need", not "bags". Defined in CoaArtisans.cpp; safe from
// any thread.
void NoteWonByRoll(Player* bot, uint32 itemEntry);

// A recipe the bot may learn now: unknown, of a profession it has, at its rank and level, with the reputation asked.
inline bool Learnable(Player* bot, ItemTemplate const* proto)
{
    uint32 const taught = TaughtSpell(proto);
    if (!bot || !taught)
        return false;
    uint32 const profession = ProfessionOf(taught);
    bool const reputationOk = !proto->RequiredReputationFaction ||
                              uint32(bot->GetReputationRank(proto->RequiredReputationFaction)) >= proto->RequiredReputationRank;
    return CoaFormulaRules::Learnable(taught, bot->HasSpell(taught), profession && bot->HasSkill(profession),
                                      bot->CanUseItem(proto) == EQUIP_ERR_OK, reputationOk);
}
}  // namespace CoaEconomy::Formulas

#endif
