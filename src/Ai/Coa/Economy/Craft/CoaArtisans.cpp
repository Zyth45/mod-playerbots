/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaArtisans.h"

#include "CoaAuctionHouseBot.h"
#include "CoaEconomyData.h"
#include "CoaEconomyRules.h"
#include "CoaMarketPrices.h"
#include "CoaSellable.h"
#include "Config.h"
#include "DBCStores.h"
#include "Item.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "WorldSession.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace CoaEconomy::Artisans
{

namespace
{
    bool enabled = false;
    uint32 minProfitPercent = 20;
    uint32 recursiveDepth = 0; // 0 = off: reagents at their market price only

    // What makes each item, from every profession recipe of the client data; built once, read-only afterwards.
    std::unordered_map<uint32, CoaEconomyRules::CraftPath> craftPaths;
    std::once_flag craftPathsBuilt;

    void BuildCraftPaths()
    {
        for (uint32 i = 0; i < sSkillLineAbilityStore.GetNumRows(); ++i)
        {
            SkillLineAbilityEntry const* ability = sSkillLineAbilityStore.LookupEntry(i);
            if (!ability)
                continue;
            SkillLineEntry const* skill = sSkillLineStore.LookupEntry(ability->SkillLine);
            if (!skill || (skill->categoryId != SKILL_CATEGORY_PROFESSION && skill->categoryId != SKILL_CATEGORY_SECONDARY))
                continue;
            SpellInfo const* spell = sSpellMgr->GetSpellInfo(ability->Spell);
            if (!spell)
                continue;
            for (SpellEffectInfo const& effect : spell->Effects)
            {
                if (effect.Effect != SPELL_EFFECT_CREATE_ITEM || !effect.ItemType)
                    continue;
                CoaEconomyRules::CraftPath path;
                path.made = uint32(std::max(1, effect.CalcValue()));
                for (uint8 r = 0; r < MAX_SPELL_REAGENTS; ++r)
                    if (spell->Reagent[r] > 0 && spell->ReagentCount[r])
                        path.reagents.emplace_back(uint32(spell->Reagent[r]), spell->ReagentCount[r]);
                // One way per item, the first the client data lists.
                if (!path.reagents.empty())
                    craftPaths.emplace(effect.ItemType, std::move(path));
                break;
            }
        }
    }

    // The profession skill line of a recipe spell, 0 when it is not one.
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

    // A product worth selling to someone else: exists, not bound on pickup, not a quest item, has a value.
    bool IsTradable(ItemTemplate const* proto)
    {
        return proto && proto->Bonding != BIND_WHEN_PICKED_UP && proto->Bonding != BIND_QUEST_ITEM &&
               proto->Class != ITEM_CLASS_QUEST && (proto->SellPrice > 0 || proto->BuyPrice > 0);
    }
}

bool IsEnabled()
{
    return enabled;
}

void LoadSettings()
{
    enabled = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaEconomy.Artisans.Enable", false, false);
    minProfitPercent = sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaEconomy.Artisans.MinProfitPercent", 20);
    recursiveDepth = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaEconomy.Artisans.RecursiveCost.Enable", false)
        ? std::min<uint32>(5, sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaEconomy.Artisans.RecursiveCost.Depth", 3))
        : 0;
}

std::string ProfessionName(Player* bot, uint32 skillId)
{
    SkillLineEntry const* skill = sSkillLineStore.LookupEntry(skillId);
    if (!skill)
        return "";
    uint8 const locale = bot && bot->GetSession() ? uint8(bot->GetSession()->GetSessionDbcLocale()) : 0;
    char const* name = skill->name[locale];
    if (!name || !*name)
        name = skill->name[0];
    return name ? name : "";
}

namespace
{
    char const* SpellNameFor(Player* bot, SpellInfo const* spell)
    {
        uint8 const locale = bot && bot->GetSession() ? uint8(bot->GetSession()->GetSessionDbcLocale()) : 0;
        char const* name = spell->SpellName[locale];
        if (!name || !*name)
            name = spell->SpellName[0];
        return name ? name : "";
    }
}

std::string ProfessionLink(Player* bot, uint32 skillId)
{
    if (!bot)
        return "";
    // The rank spells (Apprentice to Artisan) teach the skill with a tier: the highest one the bot knows.
    SpellInfo const* best = nullptr;
    int32 bestTier = -1;
    for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
    {
        if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED || !playerSpell->Active)
            continue;
        SpellInfo const* spell = sSpellMgr->GetSpellInfo(spellId);
        if (!spell || !spell->HasEffect(SPELL_EFFECT_TRADE_SKILL))
            continue;
        for (SpellEffectInfo const& effect : spell->Effects)
            if (effect.Effect == SPELL_EFFECT_SKILL && uint32(effect.MiscValue) == skillId && effect.CalcValue() > bestTier)
            {
                bestTier = effect.CalcValue();
                best = spell;
            }
    }
    if (!best)
        return ProfessionName(bot, skillId);
    return CoaEconomyRules::SpellChatLink(best->Id, SpellNameFor(bot, best), false);
}

std::string RecipeLink(Player* bot, Recipe const& recipe)
{
    if (!recipe.spell)
        return "";
    return CoaEconomyRules::SpellChatLink(recipe.spell->Id, SpellNameFor(bot, recipe.spell), true);
}

uint64 MarketPrice(uint32 itemId)
{
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
    if (!proto)
        return 0;
    if (MarketBand const* band = sCoaMarketPrices->GetBand(itemId))
        if (band->Usual)
            return band->Usual;
    if (uint64 house = auctionbot->GetReferencePricePerItem(proto))
        return house;
    // A vendor sells it: what it costs there; otherwise four times what a vendor pays for it.
    if (proto->BuyPrice)
        return proto->BuyPrice;
    return std::max<uint64>(1, uint64(proto->SellPrice) * 4);
}

uint64 ReagentCost(uint32 itemId)
{
    if (!recursiveDepth)
        return MarketPrice(itemId);
    std::call_once(craftPathsBuilt, BuildCraftPaths);
    return CoaEconomyRules::RecursiveCost(itemId, recursiveDepth, [](uint32 id) { return MarketPrice(id); },
        [](uint32 id) -> CoaEconomyRules::CraftPath const*
        {
            auto found = craftPaths.find(id);
            return found == craftPaths.end() ? nullptr : &found->second;
        });
}

std::vector<Recipe> KnownRecipes(Player* bot)
{
    std::vector<Recipe> recipes;
    if (!enabled || !bot)
        return recipes;

    for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
    {
        if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED || !playerSpell->Active)
            continue;
        SpellInfo const* spell = sSpellMgr->GetSpellInfo(spellId);
        if (!spell)
            continue;

        uint32 itemId = 0;
        uint32 count = 1;
        for (SpellEffectInfo const& effect : spell->Effects)
            if (effect.Effect == SPELL_EFFECT_CREATE_ITEM && effect.ItemType)
            {
                itemId = effect.ItemType;
                count = uint32(std::max(1, effect.CalcValue()));
                break;
            }
        // Nothing CoA does not have (Netherweave bandages), no ammunition, no vendor good.
        ItemTemplate const* product = sObjectMgr->GetItemTemplate(itemId);
        if (!itemId || !IsTradable(product) || !Sellable::BotMayList(product, true))
            continue;
        uint32 const skillId = ProfessionOf(spellId);
        if (!skillId)
            continue;

        Recipe recipe;
        recipe.skillId = skillId;
        recipe.spell = spell;
        recipe.itemId = itemId;
        recipe.count = count;
        bool reagentsKnown = true;
        for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
        {
            if (spell->Reagent[i] <= 0 || !spell->ReagentCount[i])
                continue;
            uint64 const price = ReagentCost(uint32(spell->Reagent[i]));
            if (!price)
            {
                reagentsKnown = false;
                break;
            }
            recipe.materialCost += price * spell->ReagentCount[i];
        }
        if (!reagentsKnown)
            continue;
        recipe.productValue = MarketPrice(itemId) * count;
        recipes.push_back(recipe);
    }
    return recipes;
}

bool PickProfitable(Player* bot, Recipe& out)
{
    std::vector<Recipe> profitable;
    for (Recipe const& recipe : KnownRecipes(bot))
        if (CoaEconomyRules::IsProfitable(recipe.productValue, recipe.materialCost, minProfitPercent))
            profitable.push_back(recipe);
    if (profitable.empty())
        return false;

    // The best margins, then one of the first three at random.
    std::sort(profitable.begin(), profitable.end(), [](Recipe const& a, Recipe const& b)
    {
        return a.productValue - a.materialCost > b.productValue - b.materialCost;
    });
    out = profitable[urand(0, uint32(std::min<std::size_t>(3, profitable.size())) - 1)];
    return true;
}

Item* Craft(Player* bot, Recipe const& recipe)
{
    if (!enabled || !bot || !recipe.spell)
        return nullptr;

    ItemPosCountVec dest;
    if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, recipe.itemId, recipe.count) != EQUIP_ERR_OK)
        return nullptr;

    // What the missing reagents cost, bought at market price.
    uint64 toPay = 0;
    for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        if (recipe.spell->Reagent[i] <= 0 || !recipe.spell->ReagentCount[i])
            continue;
        uint32 const reagent = uint32(recipe.spell->Reagent[i]);
        uint32 const need = recipe.spell->ReagentCount[i];
        uint32 const held = std::min(need, bot->GetItemCount(reagent));
        toPay += ReagentCost(reagent) * (need - held);
    }
    if (toPay > bot->GetMoney())
        return nullptr;

    for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        if (recipe.spell->Reagent[i] <= 0 || !recipe.spell->ReagentCount[i])
            continue;
        uint32 const reagent = uint32(recipe.spell->Reagent[i]);
        uint32 const held = std::min<uint32>(recipe.spell->ReagentCount[i], bot->GetItemCount(reagent));
        if (held)
            bot->DestroyItemCount(reagent, held, true);
    }
    if (toPay)
        bot->ModifyMoney(-int32(std::min<uint64>(toPay, uint64(INT32_MAX))));

    Item* item = bot->StoreNewItem(dest, recipe.itemId, true, Item::GenerateItemRandomPropertyId(recipe.itemId));
    if (item)
        Data::RecordCraft(recipe.skillId, recipe.itemId, recipe.count, int64(recipe.productValue) - int64(recipe.materialCost));
    return item;
}

} // namespace CoaEconomy::Artisans

namespace
{
    class CoaArtisansWorld : public WorldScript
    {
    public:
        CoaArtisansWorld() : WorldScript("CoaArtisansWorld", {WORLDHOOK_ON_AFTER_CONFIG_LOAD}) { }
        void OnAfterConfigLoad(bool /*reload*/) override { CoaEconomy::Artisans::LoadSettings(); }
    };
}

void AddSC_coa_economy_artisans()
{
    // Off by default; read at start-up (restart to change it). MinProfitPercent is read again on reload.
    if (!sConfigMgr->GetOption<bool>("AiPlayerbot.CoaEconomy.Artisans.Enable", false, false))
        return;
    CoaEconomy::Artisans::LoadSettings();
    new CoaArtisansWorld();
}
