/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaProfessions.h"

#include "CoaFormulas.h"
#include "CoaProfessionRules.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotFactory.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <mutex>
#include <unordered_set>
#include <vector>

namespace
{
    // What the world gives (loot of creatures, objects, fishing, skinning, pickpocketing, containers, disenchanting,
    // one level of references from these, quest rewards). Not milling nor prospecting: those are the retired
    // professions' own; not vendors.
    constexpr char const* WorldItemsQuery =
        "SELECT Item FROM creature_loot_template WHERE Reference = 0 "
        "UNION SELECT Item FROM gameobject_loot_template WHERE Reference = 0 "
        "UNION SELECT Item FROM fishing_loot_template WHERE Reference = 0 "
        "UNION SELECT Item FROM skinning_loot_template WHERE Reference = 0 "
        "UNION SELECT Item FROM pickpocketing_loot_template WHERE Reference = 0 "
        "UNION SELECT Item FROM item_loot_template WHERE Reference = 0 "
        "UNION SELECT Item FROM disenchant_loot_template WHERE Reference = 0 "
        "UNION SELECT r.Item FROM reference_loot_template r WHERE r.Reference = 0 AND r.Entry IN ("
        "SELECT Reference FROM creature_loot_template UNION SELECT Reference FROM gameobject_loot_template "
        "UNION SELECT Reference FROM fishing_loot_template UNION SELECT Reference FROM skinning_loot_template "
        "UNION SELECT Reference FROM pickpocketing_loot_template UNION SELECT Reference FROM item_loot_template "
        "UNION SELECT Reference FROM disenchant_loot_template) "
        "UNION SELECT RewardItem1 FROM quest_template UNION SELECT RewardItem2 FROM quest_template "
        "UNION SELECT RewardItem3 FROM quest_template UNION SELECT RewardItem4 FROM quest_template "
        "UNION SELECT RewardChoiceItemID1 FROM quest_template UNION SELECT RewardChoiceItemID2 FROM quest_template "
        "UNION SELECT RewardChoiceItemID3 FROM quest_template UNION SELECT RewardChoiceItemID4 FROM quest_template "
        "UNION SELECT RewardChoiceItemID5 FROM quest_template UNION SELECT RewardChoiceItemID6 FROM quest_template";

    CoaProfessionRules::Throttle pace;
    uint32 droppedInscription = 0, droppedJewelcrafting = 0, lastSummary = 0;

    std::once_flag itemsOnce;
    std::unordered_set<uint32> retiredItems;  // built once (the spell and item stores are fixed)

    bool IsProfession(uint32 skillId)
    {
        SkillLineEntry const* skill = sSkillLineStore.LookupEntry(skillId);
        return skill && (skill->categoryId == SKILL_CATEGORY_PROFESSION || skill->categoryId == SKILL_CATEGORY_SECONDARY);
    }

    void BuildItems()
    {
        std::vector<uint32> products, reagents, recipeItems;
        std::unordered_set<uint32> otherProducts, otherReagents, world;
        for (uint32 i = 0; i < sSkillLineAbilityStore.GetNumRows(); ++i)
        {
            SkillLineAbilityEntry const* ability = sSkillLineAbilityStore.LookupEntry(i);
            if (!ability || !IsProfession(ability->SkillLine))
                continue;
            SpellInfo const* spell = sSpellMgr->GetSpellInfo(ability->Spell);
            if (!spell)
                continue;
            bool const retired = CoaProfessionRules::Retired(ability->SkillLine);
            for (SpellEffectInfo const& effect : spell->Effects)
                if (effect.Effect == SPELL_EFFECT_CREATE_ITEM && effect.ItemType)
                {
                    if (retired)
                        products.push_back(effect.ItemType);
                    else
                        otherProducts.insert(effect.ItemType);
                }
            for (uint8 r = 0; r < MAX_SPELL_REAGENTS; ++r)
                if (spell->Reagent[r] > 0)
                {
                    if (retired)
                        reagents.push_back(uint32(spell->Reagent[r]));
                    else
                        otherReagents.insert(uint32(spell->Reagent[r]));
                }
        }
        for (auto const& [entry, proto] : *sObjectMgr->GetItemTemplateStore())
            if (proto.Class == ITEM_CLASS_RECIPE)
                if (uint32 const taught = CoaEconomy::Formulas::TaughtSpell(&proto))
                    if (CoaProfessionRules::Retired(CoaEconomy::Formulas::ProfessionOf(taught)))
                        recipeItems.push_back(entry);
        if (QueryResult result = WorldDatabase.Query(WorldItemsQuery))
            do
            {
                world.insert(result->Fetch()[0].Get<uint32>());
            } while (result->NextRow());

        retiredItems = CoaProfessionRules::RetiredItems(products, reagents, recipeItems, otherProducts, otherReagents, world);
        // Side effect to watch: retired items another profession's recipe uses (none expected in classic).
        uint32 usedByOthers = 0;
        for (uint32 id : retiredItems)
            usedByOthers += otherReagents.count(id);
        LOG_INFO("playerbots.coa", "CoaEconomy: {} Inscription and Jewelcrafting items the bots no longer make or list "
                 "({} of them a reagent of another profession)", retiredItems.size(), usedByOthers);
    }
}

namespace CoaEconomy::Professions
{
bool Retired(uint32 skillId)
{
    return CoaProfessionRules::Retired(skillId);
}

bool IsRetiredItem(ItemTemplate const* proto)
{
    if (!proto)
        return false;
    std::call_once(itemsOnce, BuildItems);
    return retiredItems.count(proto->ItemId) != 0;
}

bool RetireIfDue(Player* bot)
{
    if (!bot)
        return false;
    uint32 const now = uint32(GameTime::GetGameTime().count());
    if (lastSummary + HOUR <= now)
    {
        if (droppedInscription || droppedJewelcrafting)
            LOG_INFO("playerbots.coa", "CoaEconomy: professions (last hour): {} dropped Inscription, {} dropped "
                     "Jewelcrafting", droppedInscription, droppedJewelcrafting);
        droppedInscription = droppedJewelcrafting = 0;
        lastSummary = now;
    }
    bool const inscription = bot->HasSkill(CoaProfessionRules::Inscription);
    bool const jewelcrafting = bot->HasSkill(CoaProfessionRules::Jewelcrafting);
    if (!inscription && !jewelcrafting)
        return false;
    // A few bots a minute: a showcase had 650 of them.
    if (!pace.Take(now, MINUTE, 6))
        return false;
    if (!PlayerbotFactory::RetireProfessions(bot))
        return false;
    droppedInscription += inscription && !bot->HasSkill(CoaProfessionRules::Inscription);
    droppedJewelcrafting += jewelcrafting && !bot->HasSkill(CoaProfessionRules::Jewelcrafting);
    return true;
}
}  // namespace CoaEconomy::Professions
