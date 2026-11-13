/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaRealPrices.h"

#include "CoaEconomyConf.h"

#include "CoaEconomyRules.h"
#include "Config.h"
#include "ItemTemplate.h"
#include "ObjectMgr.h"
#include "SharedDefines.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace CoaEconomy::RealPrices
{

namespace
{
    std::unordered_map<uint32, uint64> const& Table()
    {
        static std::unordered_map<uint32, uint64> const table = {
#include "CoaRealPrices.inc"
        };
        return table;
    }

    constexpr uint32 BandLevels = 5;
    constexpr std::size_t MinSamples = 5;

    bool IsGear(ItemTemplate const* proto)
    {
        return proto->Class == ITEM_CLASS_WEAPON || proto->Class == ITEM_CLASS_ARMOR;
    }

    // (gear, quality, item level band) -> median market value of the scanned items of our item templates. Gear and
    // goods apart: white gear of level 55 is worth far more than a runecloth of level 55. Built once.
    std::map<std::tuple<bool, uint32, uint32>, uint64> floors;
    std::once_flag floorsBuilt;

    void BuildFloors()
    {
        std::map<std::tuple<bool, uint32, uint32>, std::vector<uint64>> samples;
        for (auto const& [id, price] : Table())
            if (price)
                if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(id))
                    samples[{IsGear(proto), proto->Quality, proto->ItemLevel / BandLevels}].push_back(price);
        for (auto& [key, values] : samples)
            if (values.size() >= MinSamples)
                floors[key] = CoaEconomyRules::Median(values);
    }
}

bool Seen(uint32 itemId)
{
    return Table().count(itemId) != 0;
}

uint64 Of(uint32 itemId)
{
    auto found = Table().find(itemId);
    return found == Table().end() ? 0 : found->second;
}

// The median of the scanned items of its kind, quality and item level (any quality; 0 without enough samples).
uint64 BandOf(uint32 quality, uint32 itemLevel, bool gear)
{
    std::call_once(floorsBuilt, BuildFloors);
    auto found = floors.find({gear, quality, itemLevel / BandLevels});
    return found == floors.end() ? 0 : found->second;
}

uint64 Band(ItemTemplate const* proto)
{
    return proto ? BandOf(proto->Quality, proto->ItemLevel, IsGear(proto)) : 0;
}

uint64 QualityStep(ItemTemplate const* proto, Multipliers const& multipliers)
{
    if (!proto || (proto->Class != ITEM_CLASS_WEAPON && proto->Class != ITEM_CLASS_ARMOR) ||
        proto->Quality <= ITEM_QUALITY_UNCOMMON)
        return 0;
    uint32 const lower = std::min<uint32>(proto->Quality, ITEM_QUALITY_EPIC) - 1;
    return CoaEconomyRules::QualityStep(BandOf(lower, proto->ItemLevel), multipliers.For(lower));
}

uint64 Floor(ItemTemplate const* proto)
{
    return proto && proto->Quality >= ITEM_QUALITY_UNCOMMON ? Band(proto) : 0;
}

uint64 Priced(ItemTemplate const* proto)
{
    if (!proto)
        return 0;
    // Gear stays between half and 1.5 times its median: the quality above starts there (QualityStep), so a blue never
    // outprices an epic of its item level. Goods only lose their joke prices (over 3 times): a cheap cloth or hide stays
    // cheap (the band's floor once sold a 5 s runecloth at 1.3 g); the vendor floor guards the bottom.
    // Poor and common gear has no floor either: the band's floor put white gear at 2 to 3 times the Classic Era
    // houses' prices; nothing is priced under it.
    if (IsGear(proto))
        return CoaEconomyRules::ClampToBand(Of(proto->ItemId), Band(proto), 15,
                                            proto->Quality <= ITEM_QUALITY_NORMAL ? 0 : 5);
    return CoaEconomyRules::ClampToBand(Of(proto->ItemId), Band(proto), 30, 0);
}

uint64 Reference(ItemTemplate const* proto)
{
    if (!proto)
        return 0;
    if (uint64 price = Priced(proto))
        return price;
    return Floor(proto);
}

float Multipliers::For(uint32 quality) const
{
    if (quality >= ITEM_QUALITY_EPIC)
        return epic;
    if (quality == ITEM_QUALITY_RARE)
        return rare;
    if (quality == ITEM_QUALITY_UNCOMMON)
        return uncommon;
    return other;
}

Multipliers LoadMultipliers(std::string const& prefix)
{
    Multipliers m;
    m.other = std::max(0.0f, CoaEconomy::Conf::Get<float>(prefix + ".Common", m.other));
    m.uncommon = std::max(0.0f, CoaEconomy::Conf::Get<float>(prefix + ".Uncommon", m.uncommon));
    m.rare = std::max(0.0f, CoaEconomy::Conf::Get<float>(prefix + ".Rare", m.rare));
    m.epic = std::max(0.0f, CoaEconomy::Conf::Get<float>(prefix + ".Epic", m.epic));
    return m;
}

namespace
{
    std::atomic<uint32> priceScale{100};
}

void LoadPriceScale()
{
    priceScale = CoaEconomyRules::ClampPriceScale(
        CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.PriceScalePercent", 60));
}

uint32 PriceScalePercent()
{
    return priceScale;
}

uint64 Scaled(uint64 price, ItemTemplate const* proto, Multipliers const& multipliers)
{
    if (!proto)
        return 0;
    return uint64(double(price) * (IsGear(proto) ? multipliers.For(proto->Quality) : multipliers.other));
}

} // namespace CoaEconomy::RealPrices
