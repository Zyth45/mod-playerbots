/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaRealPrices.h"

#include "CoaEconomyRules.h"
#include "Config.h"
#include "ItemTemplate.h"
#include "ObjectMgr.h"
#include "SharedDefines.h"

#include <algorithm>
#include <map>
#include <mutex>
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

    // (quality, item level band) -> median market value of the scanned items of our item templates. Built once.
    std::map<std::pair<uint32, uint32>, uint64> floors;
    std::once_flag floorsBuilt;

    void BuildFloors()
    {
        std::map<std::pair<uint32, uint32>, std::vector<uint64>> samples;
        for (auto const& [id, price] : Table())
            if (price)
                if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(id))
                    samples[{proto->Quality, proto->ItemLevel / BandLevels}].push_back(price);
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

// The median of the scanned items of its quality and item level (any quality; 0 without enough samples).
uint64 Band(ItemTemplate const* proto)
{
    if (!proto)
        return 0;
    std::call_once(floorsBuilt, BuildFloors);
    auto found = floors.find({proto->Quality, proto->ItemLevel / BandLevels});
    return found == floors.end() ? 0 : found->second;
}

uint64 Floor(ItemTemplate const* proto)
{
    return proto && proto->Quality >= ITEM_QUALITY_UNCOMMON ? Band(proto) : 0;
}

uint64 Priced(ItemTemplate const* proto)
{
    if (!proto)
        return 0;
    return CoaEconomyRules::ClampToBand(Of(proto->ItemId), Band(proto));
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
    m.other = std::max(0.0f, sConfigMgr->GetOption<float>(prefix + ".Common", m.other));
    m.uncommon = std::max(0.0f, sConfigMgr->GetOption<float>(prefix + ".Uncommon", m.uncommon));
    m.rare = std::max(0.0f, sConfigMgr->GetOption<float>(prefix + ".Rare", m.rare));
    m.epic = std::max(0.0f, sConfigMgr->GetOption<float>(prefix + ".Epic", m.epic));
    return m;
}

uint64 Scaled(uint64 price, ItemTemplate const* proto, Multipliers const& multipliers)
{
    return proto ? uint64(double(price) * multipliers.For(proto->Quality)) : 0;
}

} // namespace CoaEconomy::RealPrices
