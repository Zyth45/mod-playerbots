/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaArtisans.h"

#include "CoaEconomyConf.h"
#include "CoaEnchantSelfRules.h"
#include "CoaGearScore.h"
#include "CoaMissionMgr.h"
#include "CoaSelfStudyRules.h"

#include "CoaAuctionHouseBot.h"
#include "CoaEconomyData.h"
#include "CoaLaunchCurve.h"
#include "CoaEconomyRules.h"
#include "CoaMarketPrices.h"
#include "CoaRareRecipes.h"
#include "CoaRealPrices.h"
#include "CoaSellable.h"
#include "Config.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "Item.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "Playerbots.h"
#include "Random.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StatsWeightCalculator.h"
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
    bool realPrices = false;   // BotAuctions.RealHousePrices: market prices are fair prices
    RealPrices::Multipliers realMultipliers;

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

    // 3.2.1: an item a bot made carries its maker, as a player's does ("Made by", the crafted count): the bot's
    // crafts had no creator at all (0 creatorGuid in the showcase's database).
    Item* SignedByCrafter(Player* bot, Item* item)
    {
        if (item && bot)
            item->SetGuidValue(ITEM_FIELD_CREATOR, bot->GetGUID());
        return item;
    }
}

bool IsEnabled()
{
    return enabled;
}

bool IsTradableProduct(ItemTemplate const* proto)
{
    return IsTradable(proto);
}

bool craftForSelf = true;      // AiPlayerbot.CoaEconomy.CraftForSelf.Enable
bool craftForSelfLog = false;  // AiPlayerbot.CoaEconomy.CraftForSelf.Log
bool enchantForSelf = true;     // AiPlayerbot.CoaEconomy.EnchantForSelf.Enable
bool enchantForSelfLog = false; // AiPlayerbot.CoaEconomy.EnchantForSelf.Log
uint32 enchantPieces = 2;      // AiPlayerbot.CoaEconomy.EnchantForSelf.PiecesPerStudy (3.2.2)
bool studyRestAreas = true;    // AiPlayerbot.CoaEconomy.SelfStudy.RestAreas (3.2.2)
bool enchantReagents = true;   // AiPlayerbot.CoaEconomy.EnchantForSelf.Reagents (3.2.2)
bool enchantHourly = true;     // AiPlayerbot.CoaEconomy.EnchantForSelf.HourlyCount (3.2.2)
// 3.2.2: the enchanters' counts since the last hourly line (world thread only).
struct EnchantCounters
{
    uint32 tries = 0, enchanted = 0, disenchanted = 0;
    uint32 refusals[uint32(CoaEnchantSelfRules::Refusal::Count)] = {};
} enchantCounters;
uint32 lastEnchantSummary = 0;
// AiPlayerbot.CoaEconomy.SelfStudy.*
bool studyEnabled = true;
uint32 studyIntervalSeconds = 3600;
bool studyBuy = true;
uint32 studyMoneyPercent = 10;
uint32 studyCopperPerLevel = 2500;
uint32 studyMinGainPercent = 5;
bool studyLog = false;
// Counters since the last summary (world thread only).
struct StudyCounters
{
    uint32 studies = 0, crafted = 0, enchanted = 0, noReagents = 0, vendorUnits = 0, auctionUnits = 0;
    uint64 spent = 0;
} studyCounters;

void LoadSettings()
{
    enabled = CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.Artisans.Enable", true, false);
    craftForSelf = CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.CraftForSelf.Enable", true, false);
    craftForSelfLog = CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.CraftForSelf.Log", false, false);
    enchantForSelf = CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.EnchantForSelf.Enable", true, false);
    enchantForSelfLog = CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.EnchantForSelf.Log", false, false);
    enchantPieces = std::clamp<uint32>(
        CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.EnchantForSelf.PiecesPerStudy", 2), 1, 4);
    studyRestAreas = CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.SelfStudy.RestAreas", true);
    enchantReagents = CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.EnchantForSelf.Reagents", true);
    enchantHourly = CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.EnchantForSelf.HourlyCount", true);
    studyEnabled = CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.SelfStudy.Enable", true, false);
    studyIntervalSeconds =
        60 * std::max<uint32>(5, CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.SelfStudy.IntervalMinutes", 60));
    studyBuy = CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.SelfStudy.BuyReagents", true, false);
    studyMoneyPercent =
        std::min<uint32>(100, CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.SelfStudy.MaxGoldPercent", 10));
    studyCopperPerLevel = CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.SelfStudy.MaxCopperPerLevel", 2500);
    studyMinGainPercent = CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.SelfStudy.MinGainPercent", 5);
    studyLog = CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.SelfStudy.Log", false, false);
    minProfitPercent = CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.Artisans.MinProfitPercent", 20);
    realPrices = CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.BotAuctions.RealHousePrices", true);
    realMultipliers = RealPrices::LoadMultipliers("AiPlayerbot.CoaEconomy.BotAuctions.RealPriceMultiplier");
    recursiveDepth = CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.Artisans.RecursiveCost.Enable", true)
        ? std::min<uint32>(5, CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.Artisans.RecursiveCost.Depth", 3))
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

uint64 FairPrice(ItemTemplate const* proto, RealPrices::Multipliers const& multipliers)
{
    if (!proto)
        return 0;
    std::call_once(craftPathsBuilt, BuildCraftPaths);
    // Market values at the house-wide scale (PriceScalePercent); the vendor floor and cap come after it.
    uint32 const scale = RealPrices::PriceScalePercent();
    auto value = [&](ItemTemplate const* item)
    {
        return CoaEconomyRules::ScalePrice(RealPrices::Scaled(RealPrices::Reference(item), item, multipliers), scale);
    };
    // Its reagents at the same prices, per item made (one level).
    uint64 reagents = 0;
    if (auto path = craftPaths.find(proto->ItemId); path != craftPaths.end())
    {
        bool known = true;
        for (auto const& [reagent, count] : path->second.reagents)
        {
            ItemTemplate const* reagentProto = sObjectMgr->GetItemTemplate(reagent);
            uint64 const each = CoaEconomyRules::FairPrice(value(reagentProto), 0, reagentProto ? reagentProto->SellPrice : 0,
                Sellable::IsFreeVendorItem(reagent) && reagentProto ? reagentProto->BuyPrice : 0);
            if (!each)
            {
                known = false;
                break;
            }
            reagents += each * count;
        }
        reagents = known ? reagents / std::max<uint32>(1, path->second.made) : 0;
    }
    uint64 own = value(proto);
    // A better quality over the one below at the same item level (scans hold dumped epics).
    // Also when unseen with no epic band to floor it: else a bot fell back to a learned or vendor price (seen:
    // epics of level 62 at 71 g, under blues at 272 g).
    if (uint64 step = CoaEconomyRules::ScalePrice(RealPrices::QualityStep(proto, multipliers), scale); own < step)
        own = step;
    return CoaEconomyRules::FairPrice(own, reagents, proto->SellPrice,
                                      Sellable::IsFreeVendorItem(proto->ItemId) ? proto->BuyPrice : 0);
}

uint64 MarketPrice(uint32 itemId)
{
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
    if (!proto)
        return 0;
    if (realPrices)
        if (uint64 fair = FairPrice(proto, realMultipliers))
            return fair;
    if (MarketBand const* band = sCoaMarketPrices->GetBand(itemId))
        if (band->Usual)
            return band->Usual;
    if (uint64 house = auctionbot->GetReferencePricePerItem(proto))
        return house;
    // A vendor sells it: what it costs there; otherwise four times what a vendor pays for it.
    if (proto->BuyPrice)
        return proto->BuyPrice;
    return std::max<uint64>(1, CoaEconomyRules::FairPrice(CoaEconomyRules::ScalePrice(uint64(proto->SellPrice) * 4,
        RealPrices::PriceScalePercent()), 0, proto->SellPrice, 0));
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
    // The rare recipes it has come to know since it was last asked (RareRecipes; nothing when off).
    RareRecipes::Maintain(bot);

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
        // Nor a recipe that needs a later expansion reagent (a level 1 kit made of Fel Iron Bars).
        if (Sellable::HasLaterReagent(spell))
            continue;
        // A new realm's launch curve holds for artisans too: no epic kit made at Enchanting 1 on its first day.
        if (!sCoaLaunchCurve->AllowsListing(*product))
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
        // Nor anything dearer than the launch curve lets one item cost.
        if (!sCoaLaunchCurve->AllowsUnitPrice(recipe.productValue / count))
            continue;
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

uint64 MissingCost(Player* bot, Recipe const& recipe, uint32 times)
{
    uint64 toPay = 0;
    if (!bot || !recipe.spell)
        return toPay;
    for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        if (recipe.spell->Reagent[i] <= 0 || !recipe.spell->ReagentCount[i])
            continue;
        uint32 const reagent = uint32(recipe.spell->Reagent[i]);
        uint32 const need = recipe.spell->ReagentCount[i] * times;
        uint32 const held = std::min(need, bot->GetItemCount(reagent));
        toPay += ReagentCost(reagent) * (need - held);
    }
    return toPay;
}

bool Consume(Player* bot, Recipe const& recipe, uint32 times, bool payLater)
{
    if (!enabled || !bot || !recipe.spell || !times)
        return false;
    uint64 const toPay = payLater ? 0 : MissingCost(bot, recipe, times);
    if (toPay > bot->GetMoney())
        return false;

    for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        if (recipe.spell->Reagent[i] <= 0 || !recipe.spell->ReagentCount[i])
            continue;
        uint32 const reagent = uint32(recipe.spell->Reagent[i]);
        uint32 const held = std::min<uint32>(recipe.spell->ReagentCount[i] * times, bot->GetItemCount(reagent));
        if (held)
            bot->DestroyItemCount(reagent, held, true);
    }
    if (toPay)
        bot->ModifyMoney(-int32(std::min<uint64>(toPay, uint64(INT32_MAX))));
    Data::RecordCraft(recipe.skillId, recipe.itemId, recipe.count * times,
                      (int64(recipe.productValue) - int64(recipe.materialCost)) * int64(times));
    return true;
}

Item* Craft(Player* bot, Recipe const& recipe)
{
    if (!enabled || !bot || !recipe.spell)
        return nullptr;

    ItemPosCountVec dest;
    if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, recipe.itemId, recipe.count) != EQUIP_ERR_OK)
        return nullptr;
    if (!Consume(bot, recipe, 1))
        return nullptr;
    return SignedByCrafter(bot,
                           bot->StoreNewItem(dest, recipe.itemId, true, Item::GenerateItemRandomPropertyId(recipe.itemId)));
}

namespace
{
    // The reagents a recipe misses in the bot's bags, and what buying them costs: a vendor's price when one sells it,
    // else the auction house's. buyable is false when one of them cannot be bought (no price).
    struct ReagentPlan
    {
        uint64 cost = 0;
        uint32 vendorUnits = 0;
        uint32 auctionUnits = 0;
        bool missing = false;
        bool buyable = true;
    };

    ReagentPlan PlanReagents(Player* bot, SpellInfo const* spell)
    {
        ReagentPlan plan;
        for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
        {
            if (spell->Reagent[i] <= 0 || !spell->ReagentCount[i])
                continue;
            uint32 const reagent = uint32(spell->Reagent[i]);
            uint32 const held = bot->GetItemCount(reagent);
            if (held >= spell->ReagentCount[i])
                continue;
            uint32 const short_ = spell->ReagentCount[i] - held;
            plan.missing = true;
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(reagent);
            uint64 const vendor = proto && proto->BuyPrice && Sellable::IsFreeVendorItem(reagent) ? proto->BuyPrice : 0;
            uint64 const unit = CoaSelfStudyRules::UnitPrice(vendor, vendor ? 0 : MarketPrice(reagent));
            if (!unit)
            {
                plan.buyable = false;
                continue;
            }
            plan.cost += unit * short_;
            (vendor ? plan.vendorUnits : plan.auctionUnits) += short_;
        }
        return plan;
    }

    // A plan the study can carry out: nothing missing, or the missing reagents bought under the cap, when worth it.
    bool Affordable(ReagentPlan const& plan, uint64 buyCap, float gain, float replacedScore)
    {
        if (!plan.missing)
            return true;
        return buyCap && plan.buyable && plan.cost <= buyCap &&
               CoaSelfStudyRules::WorthBuying(gain, replacedScore, studyMinGainPercent);
    }

    // Uses the reagents: the held ones, and the missing ones paid in gold (the bot shopped for them).
    void TakeReagents(Player* bot, SpellInfo const* spell, ReagentPlan const& plan)
    {
        for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
        {
            if (spell->Reagent[i] <= 0 || !spell->ReagentCount[i])
                continue;
            uint32 const reagent = uint32(spell->Reagent[i]);
            uint32 const held = std::min<uint32>(spell->ReagentCount[i], bot->GetItemCount(reagent));
            if (held)
                bot->DestroyItemCount(reagent, held, true);
        }
        if (!plan.missing)
            return;
        bot->ModifyMoney(-int32(std::min<uint64>(plan.cost, uint64(INT32_MAX))));
        studyCounters.vendorUnits += plan.vendorUnits;
        studyCounters.auctionUnits += plan.auctionUnits;
        studyCounters.spent += plan.cost;
    }

    // Set by the two studies when an upgrade was there but its reagents could not be had.
    bool studyShort = false;
}

Item* CraftUpgradeForSelf(Player* bot, PlayerbotAI* botAI, uint64 buyCap)
{
    if (!craftForSelf || !bot || !botAI)
        return nullptr;

    SpellInfo const* bestSpell = nullptr;
    uint32 bestItem = 0;
    float bestGain = 0.0f;
    ReagentPlan bestPlan;
    for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
    {
        if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED || !playerSpell->Active)
            continue;
        SpellInfo const* spell = sSpellMgr->GetSpellInfo(spellId);
        if (!spell || !ProfessionOf(spellId))
            continue;
        uint32 itemId = 0;
        for (SpellEffectInfo const& effect : spell->Effects)
            if (effect.Effect == SPELL_EFFECT_CREATE_ITEM && effect.ItemType)
            {
                itemId = effect.ItemType;
                break;
            }
        ItemTemplate const* proto = itemId ? sObjectMgr->GetItemTemplate(itemId) : nullptr;
        if (!proto || (proto->Class != ITEM_CLASS_WEAPON && proto->Class != ITEM_CLASS_ARMOR) ||
            proto->RequiredLevel > bot->GetLevel() || bot->CanUseItem(proto) != EQUIP_ERR_OK)
            continue;
        float const gain = CoaGearScore::UpgradeGain(bot, itemId);
        if (gain <= bestGain)
            continue;
        ReagentPlan const plan = PlanReagents(bot, spell);
        // What it replaces: the piece's own score less its gain.
        float const replaced = plan.missing ? CoaGearScore::ItemScore(bot, itemId, 0, -1) - gain : 0.0f;
        if (!Affordable(plan, buyCap, gain, replaced))
        {
            studyShort = true;
            continue;
        }
        bestGain = gain;
        bestSpell = spell;
        bestItem = itemId;
        bestPlan = plan;
    }
    if (!bestSpell)
        return nullptr;

    ItemPosCountVec dest;
    if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, bestItem, 1) != EQUIP_ERR_OK)
        return nullptr;
    TakeReagents(bot, bestSpell, bestPlan);
    Item* item = SignedByCrafter(bot, bot->StoreNewItem(dest, bestItem, true, Item::GenerateItemRandomPropertyId(bestItem)));
    if (!item)
        return nullptr;
    // Worn at once, where it is an upgrade.
    uint8 const slot = botAI->FindEquipSlot(item->GetTemplate(), NULL_SLOT, true);
    if (slot != NULL_SLOT)
        CoaMissionMgr::EquipItem(bot, item, slot);
    ++studyCounters.crafted;
    if (craftForSelfLog)
        LOG_INFO("playerbots.loot", "craft for self: {} (level {}) made {} ({}), gain {:.1f}{}, bought {} reagent(s) for {}c",
                 bot->GetName(), bot->GetLevel(), bestItem, item->GetTemplate()->Name1, bestGain,
                 slot != NULL_SLOT ? ", worn" : ", kept", bestPlan.vendorUnits + bestPlan.auctionUnits, bestPlan.cost);
    return item;
}

namespace
{
    // 3.2.2: the tools the spell asks for, held as the game checks them for a player (a runed rod for most
    // enchantments: TotemCategory).
    bool HoldsTools(Player* bot, SpellInfo const* spell)
    {
        auto const item = [bot](uint32 id) { return id && bot->HasItemCount(id); };
        auto const category = [bot](uint32 id) { return id && bot->HasItemTotemCategory(id); };
        return CoaEnchantSelfRules::HasTools(spell->Totem[0], item(spell->Totem[0]), spell->Totem[1],
                                             item(spell->Totem[1])) &&
               CoaEnchantSelfRules::HasTools(spell->TotemCategory[0], category(spell->TotemCategory[0]),
                                             spell->TotemCategory[1], category(spell->TotemCategory[1]));
    }

    bool EnchantOnce(Player* bot, uint64 buyCap, bool countRefusal);

    // The permanent enchantment a known spell puts on, when the bot's level and skill allow it; null otherwise.
    SpellItemEnchantmentEntry const* UsablePermanentEnchant(Player* bot, SpellInfo const* spell, uint32& enchantId)
    {
        enchantId = 0;
        for (SpellEffectInfo const& effect : spell->Effects)
            if (effect.Effect == SPELL_EFFECT_ENCHANT_ITEM && effect.MiscValue)
            {
                enchantId = uint32(effect.MiscValue);
                break;
            }
        SpellItemEnchantmentEntry const* enchant = enchantId ? sSpellItemEnchantmentStore.LookupEntry(enchantId) : nullptr;
        if (!enchant || enchant->slot != PERM_ENCHANTMENT_SLOT ||
            !CoaEnchantSelfRules::Usable(bot->GetLevel(), enchant->requiredLevel, enchant->requiredSkill,
                                         enchant->requiredSkill ? bot->GetSkillValue(enchant->requiredSkill) : 0,
                                         enchant->requiredSkillValue))
            return nullptr;
        return enchant;
    }
}

std::unordered_set<uint32> EnchantReagents(Player* bot)
{
    std::unordered_set<uint32> reagents;
    if (!enchantForSelf || !enchantReagents || !bot || !bot->HasSkill(SKILL_ENCHANTING))
        return reagents;
    for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
    {
        if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED || !playerSpell->Active)
            continue;
        SpellInfo const* spell = sSpellMgr->GetSpellInfo(spellId);
        uint32 enchantId = 0;
        if (!spell || !UsablePermanentEnchant(bot, spell, enchantId))
            continue;
        for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
            if (spell->Reagent[i] > 0 && spell->ReagentCount[i])
                reagents.insert(uint32(spell->Reagent[i]));
    }
    return reagents;
}

bool KeepsForEnchanting(Player* bot, Item* item, std::unordered_set<uint32> const& reagents)
{
    if (!bot || !item || !reagents.count(item->GetEntry()))
        return false;
    return CoaEnchantSelfRules::KeepsReagent(bot->GetItemCount(item->GetEntry()), item->GetTemplate()->GetMaxStackSize());
}

bool ShortOfEnchantReagents(Player* bot, std::unordered_set<uint32> const& reagents)
{
    for (uint32 reagent : reagents)
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(reagent);
        if (proto && bot->GetItemCount(reagent) < std::max<uint32>(1, proto->GetMaxStackSize()))
            return true;
    }
    return false;
}

bool EnchantForSelf(Player* bot, uint64 buyCap, uint32 pieces)
{
    if (!enchantForSelf || !bot || !bot->HasSkill(SKILL_ENCHANTING))
        return false;
    ++enchantCounters.tries;
    uint32 done = 0;
    // The cause is counted for the first piece only: a second one not found is no refusal.
    while (done < std::max<uint32>(1, pieces) && EnchantOnce(bot, buyCap, done == 0))
        ++done;
    return done > 0;
}

namespace
{
bool EnchantOnce(Player* bot, uint64 buyCap, bool countRefusal)
{
    bool anyUsable = false, reachedPieces = false, goldShort = false, reagentShort = false, rodShort = false;
    StatsWeightCalculator calculator(bot);
    Item* bestItem = nullptr;
    SpellInfo const* bestSpell = nullptr;
    uint32 bestEnchant = 0;
    float bestGain = 0.0f;
    ReagentPlan bestPlan;
    for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
    {
        if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED || !playerSpell->Active)
            continue;
        SpellInfo const* spell = sSpellMgr->GetSpellInfo(spellId);
        if (!spell)
            continue;
        uint32 enchantId = 0;
        SpellItemEnchantmentEntry const* enchant = UsablePermanentEnchant(bot, spell, enchantId);
        if (!enchant)
            continue;
        if (enchant->EnchantmentCondition && !bot->EnchantmentFitsRequirements(enchant->EnchantmentCondition, -1))
            continue;
        anyUsable = true;
        if (!HoldsTools(bot, spell))
        {
            rodShort = true;
            continue;
        }
        ReagentPlan const plan = PlanReagents(bot, spell);
        if (plan.missing && (!buyCap || !plan.buyable || plan.cost > buyCap))
        {
            studyShort = true;
            (buyCap && plan.buyable ? goldShort : reagentShort) = true;
            continue;
        }
        reachedPieces = true;
        float const score = calculator.CalculateEnchant(enchantId);
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (!item || !item->IsFitToSpellRequirements(spell))
                continue;
            uint32 const current = item->GetEnchantmentId(PERM_ENCHANTMENT_SLOT);
            float const currentScore = current ? calculator.CalculateEnchant(current) : 0.0f;
            if (!CoaEnchantSelfRules::Replaces(score, currentScore))
                continue;
            float const gain = score - currentScore;
            // Bought reagents: worth it against the piece the enchantment goes on.
            if (plan.missing &&
                !CoaSelfStudyRules::WorthBuying(gain, CoaGearScore::ItemScore(bot, item->GetEntry(),
                                                    item->GetInt32Value(ITEM_FIELD_RANDOM_PROPERTIES_ID), slot),
                                                studyMinGainPercent))
                continue;
            if (gain > bestGain)
            {
                bestGain = gain;
                bestItem = item;
                bestSpell = spell;
                bestEnchant = enchantId;
                bestPlan = plan;
            }
        }
    }
    if (!bestItem)
    {
        if (countRefusal)
            ++enchantCounters.refusals[uint32(
                CoaEnchantSelfRules::Cause(anyUsable, reachedPieces, goldShort, reagentShort, rodShort))];
        return false;
    }

    TakeReagents(bot, bestSpell, bestPlan);
    ++enchantCounters.enchanted;
    bot->ApplyEnchantment(bestItem, PERM_ENCHANTMENT_SLOT, false);
    bestItem->SetEnchantment(PERM_ENCHANTMENT_SLOT, bestEnchant, 0, 0, bot->GetGUID());
    bot->ApplyEnchantment(bestItem, PERM_ENCHANTMENT_SLOT, true);
    ++studyCounters.enchanted;
    if (enchantForSelfLog)
        LOG_INFO("playerbots.loot", "enchant for self: {} (level {}) put enchantment {} on {} ({}), gain {:.1f}, bought {} reagent(s) for {}c",
                 bot->GetName(), bot->GetLevel(), bestEnchant, bestItem->GetEntry(), bestItem->GetTemplate()->Name1,
                 bestGain, bestPlan.vendorUnits + bestPlan.auctionUnits, bestPlan.cost);
    return true;
}
}

namespace
{
    // When each bot studies next (game time, seconds); bounded by the number of bots.
    std::unordered_map<uint64, uint32> nextStudy;
    uint32 lastSummary = 0;

    bool InCapital(Player* bot)
    {
        AreaTableEntry const* zone = sAreaTableStore.LookupEntry(bot->GetZoneId());
        return zone && (zone->flags & AREA_FLAG_CAPITAL);
    }

    void Summary(uint32 now)
    {
        if (!lastSummary)
            lastSummary = now;
        if (now < lastSummary + 3600)
            return;
        lastSummary = now;
        if (studyLog)
            LOG_INFO("playerbots.loot", "self study (last hour): {} studies, {} crafted, {} enchanted, {} short of "
                     "reagents, bought {} at a vendor and {} at the auction house for {}c",
                     studyCounters.studies, studyCounters.crafted, studyCounters.enchanted, studyCounters.noReagents,
                     studyCounters.vendorUnits, studyCounters.auctionUnits, studyCounters.spent);
        studyCounters = StudyCounters();
    }
}

bool StudyIfDue(Player* bot, PlayerbotAI* botAI)
{
    if (!studyEnabled || !bot || !botAI || !bot->IsInWorld() || !bot->IsAlive() || bot->IsInCombat() ||
        !sRandomPlayerbotMgr.IsRandomBot(bot) ||
        !CoaEnchantSelfRules::StudyPlace(InCapital(bot), studyRestAreas, bot->HasRestFlag(REST_FLAG_IN_CITY),
                                         bot->HasRestFlag(REST_FLAG_IN_TAVERN)))
        return false;
    uint32 const now = uint32(GameTime::GetGameTime().count());
    auto [it, fresh] = nextStudy.try_emplace(bot->GetGUID().GetRawValue(), 0);
    if (fresh)
    {
        // Spread over the first interval, from the guid, so a start does not make a whole city study at once.
        uint32 const spread = uint32(bot->GetGUID().GetCounter() * 2654435761u) % 1000;
        it->second = now + CoaSelfStudyRules::FirstStudyDelay(float(spread) / 1000.0f, studyIntervalSeconds);
    }
    if (now < it->second)
        return false;
    it->second = now + studyIntervalSeconds;

    ++studyCounters.studies;
    studyShort = false;
    auto cap = [bot]()
    {
        return studyBuy ? CoaSelfStudyRules::SpendCap(bot->GetMoney(), studyMoneyPercent, bot->GetLevel(),
                                                      studyCopperPerLevel)
                        : 0;
    };
    Item* const made = CraftUpgradeForSelf(bot, botAI, cap());
    bool const enchanted = EnchantForSelf(bot, cap(), enchantPieces);
    if (!made && !enchanted && studyShort)
        ++studyCounters.noReagents;
    if (studyLog)
        LOG_INFO("playerbots.loot", "self study: {} (level {}, {}c) {}{}{}", bot->GetName(), bot->GetLevel(),
                 bot->GetMoney(), made ? "crafted " : "", enchanted ? "enchanted " : "",
                 !made && !enchanted ? (studyShort ? "short of reagents" : "nothing to make") : "");
    Summary(now);
    return true;
}

namespace
{
    // Every 30 s, the random bots in a capital whose study is due (a few at a time).
    uint32 scanTimer = 0;
    void ScanCapitals(uint32 diff)
    {
        if (!studyEnabled)
            return;
        scanTimer += diff;
        if (scanTimer < 30 * IN_MILLISECONDS)
            return;
        scanTimer = 0;
        uint32 studied = 0;
        for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
        {
            if (!player || !player->IsInWorld() || player->IsBeingTeleported() || player->IsFlying() ||
                player->GetMap()->Instanceable())
                continue;
            PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);
            if (!botAI || !botAI->IsBotAI())
                continue;
            if (StudyIfDue(player, botAI) && ++studied >= 20)
                break;
        }
        Summary(uint32(GameTime::GetGameTime().count()));
    }
}

void NoteDisenchants(uint32 count)
{
    enchantCounters.disenchanted += count;
}

namespace
{
    // 3.2.2 (EnchantForSelf.HourlyCount): once an hour in playerbots.coa, what the enchanter bots did.
    void EnchantSummary()
    {
        uint32 const now = uint32(GameTime::GetGameTime().count());
        if (!lastEnchantSummary)
            lastEnchantSummary = now;
        if (now < lastEnchantSummary + 3600)
            return;
        lastEnchantSummary = now;
        if (enchantHourly && enchantForSelf)
        {
            uint32 online = 0;
            for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
                if (player && player->IsInWorld() && player->HasSkill(SKILL_ENCHANTING) && GET_PLAYERBOT_AI(player) &&
                    GET_PLAYERBOT_AI(player)->IsBotAI())
                    ++online;
            using CoaEnchantSelfRules::Refusal;
            auto const refused = [](Refusal cause) { return enchantCounters.refusals[uint32(cause)]; };
            LOG_INFO("playerbots.coa", "CoaEconomy: enchanters (last hour): {} online, {} tries, {} enchantments put "
                     "on, {} items disenchanted; nothing put on: {} no recipe, {} no rod, {} no reagent, {} no gold, "
                     "{} no gain", online, enchantCounters.tries, enchantCounters.enchanted,
                     enchantCounters.disenchanted, refused(Refusal::NoRecipe), refused(Refusal::NoRod),
                     refused(Refusal::NoReagent), refused(Refusal::NoGold), refused(Refusal::NoGain));
        }
        enchantCounters = EnchantCounters();
    }
}

void ScanCapitalsTick(uint32 diff)
{
    ScanCapitals(diff);
    EnchantSummary();
}

} // namespace CoaEconomy::Artisans

namespace
{
    class CoaArtisansWorld : public WorldScript
    {
    public:
        CoaArtisansWorld() : WorldScript("CoaArtisansWorld", {WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_UPDATE}) { }
        void OnAfterConfigLoad(bool /*reload*/) override { CoaEconomy::Artisans::LoadSettings(); }
        // World thread, between two map updates (3.2 SelfStudy).
        void OnUpdate(uint32 diff) override { CoaEconomy::Artisans::ScanCapitalsTick(diff); }
    };
}

void AddSC_coa_economy_artisans()
{
    // Off by default; read at start-up (restart to change it). MinProfitPercent is read again on reload.
    if (!CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.Artisans.Enable", true, false))
        return;
    CoaEconomy::Artisans::LoadSettings();
    new CoaArtisansWorld();
}
