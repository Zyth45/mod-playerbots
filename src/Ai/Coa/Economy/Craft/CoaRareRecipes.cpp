/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaRareRecipes.h"

#include "CoaArtisans.h"
#include "CoaEconomyConf.h"
#include "CoaEconomyRules.h"
#include "CoaSellable.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace CoaEconomy::RareRecipes
{

namespace
{
    bool enabled = false;
    uint32 percent = 15;
    uint32 epicPercent = 5;

    struct Rare
    {
        uint32 spellId;
        uint32 skillId;
        uint32 rank;         // the recipe item's required rank
        uint32 productLevel; // the product's required level
        bool epic;
    };
    // Built once at start-up, read-only afterwards.
    std::vector<Rare> pool;

    // What each bot was last checked at (its ranks and level summed): nothing is learned while it stays the same.
    std::unordered_map<uint32, uint32> checked;
    std::mutex checkedLock;

    // Recipe items the world gives: dropped (any loot table) or sold by a vendor.
    constexpr char const* FoundRecipesQuery =
        "SELECT l.Item FROM creature_loot_template l JOIN item_template i ON i.entry = l.Item AND i.class = 9 "
        "WHERE l.Reference = 0 "
        "UNION SELECT l.Item FROM reference_loot_template l JOIN item_template i ON i.entry = l.Item AND i.class = 9 "
        "WHERE l.Reference = 0 "
        "UNION SELECT l.Item FROM gameobject_loot_template l JOIN item_template i ON i.entry = l.Item AND i.class = 9 "
        "WHERE l.Reference = 0 "
        "UNION SELECT l.Item FROM item_loot_template l JOIN item_template i ON i.entry = l.Item AND i.class = 9 "
        "WHERE l.Reference = 0 "
        "UNION SELECT l.Item FROM fishing_loot_template l JOIN item_template i ON i.entry = l.Item AND i.class = 9 "
        "WHERE l.Reference = 0 "
        "UNION SELECT l.Item FROM pickpocketing_loot_template l JOIN item_template i ON i.entry = l.Item "
        "AND i.class = 9 WHERE l.Reference = 0 "
        "UNION SELECT v.item FROM npc_vendor v JOIN item_template i ON i.entry = v.item AND i.class = 9";

    // The profession skill line of a recipe spell (primary or secondary), 0 when it is none.
    uint32 ProfessionOf(uint32 spellId)
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

    // The spell a recipe item teaches (not the generic "learn" cast), 0 when none.
    uint32 TaughtSpell(ItemTemplate const& recipe)
    {
        for (auto const& spell : recipe.Spells)
            if (spell.SpellId > 0 && spell.SpellId != 483 && spell.SpellId != 55884)
                return uint32(spell.SpellId);
        return 0;
    }

    ItemTemplate const* ProductOf(SpellInfo const* spell)
    {
        for (SpellEffectInfo const& effect : spell->Effects)
            if (effect.Effect == SPELL_EFFECT_CREATE_ITEM && effect.ItemType)
                return sObjectMgr->GetItemTemplate(effect.ItemType);
        return nullptr;
    }

    void BuildPool()
    {
        std::unordered_set<uint32> found;
        if (QueryResult result = WorldDatabase.Query(FoundRecipesQuery))
            do
                found.insert(result->Fetch()[0].Get<uint32>());
            while (result->NextRow());

        std::unordered_set<uint32> seen;
        for (auto const& [entry, recipe] : *sObjectMgr->GetItemTemplateStore())
        {
            if (recipe.Class != ITEM_CLASS_RECIPE || !found.count(entry))
                continue;
            uint32 const spellId = TaughtSpell(recipe);
            SpellInfo const* spell = spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
            if (!spell || !seen.insert(spellId).second)
                continue;
            uint32 const skillId = ProfessionOf(spellId);
            ItemTemplate const* product = ProductOf(spell);
            // What the artisans may sell: tradable, of its era, no vendor good, no ammunition.
            if (!skillId || !Artisans::IsTradableProduct(product) || !Sellable::BotMayList(product, true))
                continue;
            pool.push_back({spellId, skillId, recipe.RequiredSkillRank, product->RequiredLevel,
                            product->Quality >= ITEM_QUALITY_EPIC});
        }
        LOG_INFO("server.loading", ">> CoA Economy: {} rare recipes artisans may know", pool.size());
    }
}

bool IsEnabled()
{
    return enabled;
}

void LoadSettings()
{
    percent = std::min<uint32>(100, CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.Artisans.LearnRareRecipes.Percent", 15));
    epicPercent = std::min<uint32>(100,
        CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.Artisans.LearnRareRecipes.EpicPercent", 5));
    // A new draw for every bot after a reload: the chances may have changed.
    std::lock_guard<std::mutex> lock(checkedLock);
    checked.clear();
}

void Maintain(Player* bot)
{
    if (!enabled || pool.empty() || !bot || !sRandomPlayerbotMgr.IsRandomBot(bot))
        return;

    uint32 signature = bot->GetLevel();
    for (uint32 skillId : {SKILL_ALCHEMY, SKILL_BLACKSMITHING, SKILL_ENCHANTING, SKILL_ENGINEERING, SKILL_LEATHERWORKING,
                           SKILL_TAILORING, SKILL_JEWELCRAFTING, SKILL_INSCRIPTION, SKILL_COOKING, SKILL_FIRST_AID})
        signature = signature * 31 + bot->GetSkillValue(skillId);
    {
        std::lock_guard<std::mutex> lock(checkedLock);
        auto [it, added] = checked.try_emplace(bot->GetGUID().GetCounter(), signature);
        if (!added && it->second == signature)
            return;
        it->second = signature;
    }

    uint64 const guid = bot->GetGUID().GetRawValue();
    for (Rare const& rare : pool)
    {
        if (!CoaEconomyRules::RareRecipeReachable(bot->GetSkillValue(rare.skillId), rare.rank, bot->GetLevel(),
                                                  rare.productLevel))
            continue;
        if (!CoaEconomyRules::RareRecipeDrawn(guid, rare.spellId, rare.epic ? epicPercent : percent))
            continue;
        if (!bot->HasSpell(rare.spellId))
            bot->learnSpell(rare.spellId, false);
    }
}

} // namespace CoaEconomy::RareRecipes

namespace
{
    class CoaRareRecipesWorld : public WorldScript
    {
    public:
        CoaRareRecipesWorld()
            : WorldScript("CoaRareRecipesWorld", {WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP}) { }
        void OnAfterConfigLoad(bool /*reload*/) override { CoaEconomy::RareRecipes::LoadSettings(); }
        void OnStartup() override { CoaEconomy::RareRecipes::BuildPool(); }
    };
}

void AddSC_coa_economy_rare_recipes()
{
    // Off by default; read at start-up (restart to change it). The chances are read again on reload.
    if (!CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.Artisans.LearnRareRecipes.Enable", false, false) ||
        !CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.Artisans.Enable", true, false))
        return;
    CoaEconomy::RareRecipes::enabled = true;
    CoaEconomy::RareRecipes::LoadSettings();
    new CoaRareRecipesWorld();
}
