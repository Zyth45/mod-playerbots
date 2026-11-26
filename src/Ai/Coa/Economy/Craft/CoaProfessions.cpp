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
#include "RandomPlayerbotMgr.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"

#include <mutex>
#include <unordered_set>
#include <vector>

namespace
{
    // What the world gives (loot of creatures, objects, fishing, skinning, pickpocketing, containers, disenchanting,
    // one level of references from these, quest rewards). Not milling nor prospecting: those are the retired
    // professions' own; not vendors.
    // Plain scans only, joined here: a "IN (SELECT ... UNION ...)" on reference_loot_template ran as a dependent
    // subquery and held the world's start for minutes. Each scan is capped; one that fails leaves the item filter off.
    constexpr char const* LootTables[] = {"creature_loot_template", "gameobject_loot_template", "fishing_loot_template",
                                          "skinning_loot_template", "pickpocketing_loot_template", "item_loot_template",
                                          "disenchant_loot_template"};
    constexpr char const* Capped = "SELECT /*+ MAX_EXECUTION_TIME(10000) */ ";

    // False when a scan failed: the caller then lists nothing as retired (the professions stay retired).
    bool WorldItems(std::unordered_set<uint32>& world)
    {
        std::unordered_set<uint32> references;
        bool read = true;  // creature and reference loot are never empty on a real world: no rows = a failed scan
        for (char const* table : LootTables)
        {
            QueryResult result = WorldDatabase.Query("{}Item, Reference FROM {}", Capped, table);
            if (!result)
            {
                read &= table != LootTables[0];  // the small tables may be empty
                continue;
            }
            do
            {
                Field* fields = result->Fetch();
                if (uint32 const reference = fields[1].Get<uint32>())
                    references.insert(reference);
                else
                    world.insert(fields[0].Get<uint32>());
            } while (result->NextRow());
        }
        QueryResult refs = WorldDatabase.Query("{}Entry, Item FROM reference_loot_template WHERE Reference = 0", Capped);
        read &= bool(refs);
        if (QueryResult result = refs)
            do
            {
                Field* fields = result->Fetch();
                if (references.count(fields[0].Get<uint32>()))
                    world.insert(fields[1].Get<uint32>());
            } while (result->NextRow());
        for (auto const& [id, quest] : sObjectMgr->GetQuestTemplates())
        {
            for (uint32 item : quest->RewardItemId)
                world.insert(item);
            for (uint32 item : quest->RewardChoiceItemId)
                world.insert(item);
        }
        world.erase(0);
        return read;
    }

    constexpr uint32 PerMinute = 30;
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
        uint32 const start = getMSTime();
        if (!WorldItems(world))
        {
            LOG_ERROR("playerbots.coa", "CoaEconomy: the world's loot could not be read ({} items in {} ms): bots skip "
                      "the Inscription and Jewelcrafting item filter, the professions stay retired", world.size(),
                      GetMSTimeDiffToNow(start));
            return;
        }

        retiredItems = CoaProfessionRules::RetiredItems(products, reagents, recipeItems, otherProducts, otherReagents, world);
        // Side effect to watch: retired items another profession's recipe uses (none expected in classic).
        uint32 usedByOthers = 0;
        for (uint32 id : retiredItems)
            usedByOthers += otherReagents.count(id);
        LOG_INFO("playerbots.coa", "CoaEconomy: {} Inscription and Jewelcrafting items the bots no longer make or list "
                 "({} of them a reagent of another profession), read in {} ms", retiredItems.size(), usedByOthers,
                 GetMSTimeDiffToNow(start));
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
    // Random bots only (a player's own bots keep what their player chose), and before the pace: a bot that cannot
    // change must not take the slot of one that can.
    if ((!inscription && !jewelcrafting) || !sRandomPlayerbotMgr.IsRandomBot(bot))
        return false;
    // 30 bots a minute: 3 000 bots in under two hours, never a whole city at once.
    if (!pace.Take(now, MINUTE, PerMinute))
        return false;
    if (!PlayerbotFactory::RetireProfessions(bot))
        return false;
    droppedInscription += inscription && !bot->HasSkill(CoaProfessionRules::Inscription);
    droppedJewelcrafting += jewelcrafting && !bot->HasSkill(CoaProfessionRules::Jewelcrafting);
    return true;
}
}  // namespace CoaEconomy::Professions
