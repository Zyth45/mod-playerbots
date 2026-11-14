/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAALTBAGSRULES_H
#define PLAYERBOTS_COAALTBAGSRULES_H

#include <cstdint>
#include <cstdio>
#include <array>
#include <cstdlib>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

// The rules of "alt cmp|bags|sell|destroy" (docs: alt bags protocol), without the game objects, for the unit test.
namespace CoaAltBags
{
// Above green nothing is sold or destroyed in bulk.
constexpr uint32_t MAX_QUALITY = 2;

struct Entry
{
    uint8_t bag;
    uint8_t slot;
    uint32_t itemId;
    uint32_t count;
};

// FNV-1a over the previewed list: "sell"/"destroy" go only if the bags still hold exactly what the player saw.
inline std::string Token(std::vector<Entry> const& entries)
{
    uint32_t hash = 2166136261u;
    auto mix = [&hash](uint32_t value)
    {
        for (int i = 0; i < 4; ++i)
        {
            hash ^= (value >> (8 * i)) & 0xFF;
            hash *= 16777619u;
        }
    };
    for (Entry const& e : entries)
    {
        mix(e.bag);
        mix(e.slot);
        mix(e.itemId);
        mix(e.count);
    }
    char out[9];
    std::snprintf(out, sizeof(out), "%08x", hash);
    return out;
}

// "skip 255:3,1:12": the bag and slot of each item the player unticked.
inline std::set<std::pair<uint8_t, uint8_t>> ParseSkip(std::string const& text)
{
    std::set<std::pair<uint8_t, uint8_t>> out;
    size_t pos = 0;
    while (pos < text.size())
    {
        size_t end = text.find(',', pos);
        if (end == std::string::npos)
            end = text.size();
        std::string const part = text.substr(pos, end - pos);
        size_t const colon = part.find(':');
        if (colon != std::string::npos && colon > 0 && colon + 1 < part.size())
        {
            long const bag = std::strtol(part.c_str(), nullptr, 10);
            long const slot = std::strtol(part.c_str() + colon + 1, nullptr, 10);
            if (bag >= 0 && bag <= 255 && slot >= 0 && slot <= 255)
                out.insert({uint8_t(bag), uint8_t(slot)});
        }
        pos = end + 1;
    }
    return out;
}

// An item named by id ("2589"), by item string ("item:2589:0:0:0:0:0:-12:0:60") or by a full chat link
// ("|cff...|Hitem:2589:...|h[Linen Cloth]|h|r"). The 7th field is the random property (negative: a suffix).
inline bool ParseItem(std::string const& text, uint32_t& itemId, int32_t& randomPropertyId)
{
    itemId = 0;
    randomPropertyId = 0;
    size_t start = text.find("item:");
    if (start == std::string::npos)
    {
        size_t const first = text.find_first_not_of(' ');
        if (first == std::string::npos || text[first] < '0' || text[first] > '9')
            return false;
        itemId = uint32_t(std::strtoul(text.c_str() + first, nullptr, 10));
        return itemId != 0;
    }
    start += 5;
    std::vector<long> fields;
    size_t pos = start;
    while (pos <= text.size() && fields.size() < 9)
    {
        char* end = nullptr;
        long const value = std::strtol(text.c_str() + pos, &end, 10);
        if (end == text.c_str() + pos)
            break;
        fields.push_back(value);
        pos = size_t(end - text.c_str());
        if (pos >= text.size() || text[pos] != ':')
            break;
        ++pos;
    }
    if (fields.empty() || fields[0] <= 0)
        return false;
    itemId = uint32_t(fields[0]);
    if (fields.size() > 6)
        randomPropertyId = int32_t(fields[6]);
    return true;
}

// The equipment slots (server numbering, 0 = head) an inventory type goes to. Rings, trinkets and one-hand
// weapons give both slots; a two-hander also gives the off hand (what it would take off).
// Requests waiting for the bot's update; beyond, a compare is answered "busy" at once.
constexpr size_t QUEUE_MAX = 16;

inline std::vector<uint8_t> SlotsFor(uint32_t inventoryType)
{
    switch (inventoryType)
    {
        case 1:  return {0};        // head
        case 2:  return {1};        // neck
        case 3:  return {2};        // shoulders
        case 4:  return {3};        // shirt
        case 5:
        case 20: return {4};        // chest, robe
        case 6:  return {5};        // waist
        case 7:  return {6};        // legs
        case 8:  return {7};        // feet
        case 9:  return {8};        // wrists
        case 10: return {9};        // hands
        case 11: return {10, 11};   // finger
        case 12: return {12, 13};   // trinket
        case 16: return {14};       // back
        case 13:
        case 17: return {15, 16};   // one hand, two hands
        case 21: return {15};       // main hand
        case 14:
        case 22:
        case 23: return {16};       // shield, off hand, held in off hand
        case 15:
        case 25:
        case 26:
        case 28: return {17};       // ranged, thrown, wand, relic
        case 19: return {18};       // tabard
        default: return {};
    }
}

// The bot's verdict, from its item usage (ItemUsage values): equip, replace, bad or cannot; other for non-gear.
inline char const* Verdict(bool gear, bool canUse, uint32_t usage)
{
    if (!gear)
        return "other";
    if (!canUse)
        return "cannot";
    if (usage == 1)
        return "equip";
    if (usage == 2)
        return "replace";
    return "bad";
}

// What a bag item is, for the bulk sell and destroy.
struct Candidate
{
    uint32_t quality = 0;
    bool quest = false;          // quest item, starts a quest, or bound to a quest
    bool useSpell = false;       // has an on-use spell (potions, hearthstone, openers...)
    bool upgrade = false;        // the bot would equip it
    bool kept = false;           // "ll +[item]"
    bool outfit = false;         // part of a saved outfit
    bool container = false;      // a bag, a quiver, an ammo pouch
    bool ammo = false;
    bool tool = false;           // a profession tool or a reagent the bot's skills use
    bool conjured = false;
    bool noPrice = false;        // a vendor gives nothing for it
};

// The bag slots (bagslots, bagequip, and the bag a bot puts on by itself): sizes of the four bag slots, 0 = empty.
// The slot (0..3) a new bag of `newSize` goes to: the one asked for (1..4, the player's choice, whatever its size), else
// the first empty one, else the smallest bag when the new one is bigger. -1: none.
inline int PickBagSlot(std::array<uint32_t, 4> const& sizes, uint32_t newSize, uint32_t requested)
{
    if (requested >= 1 && requested <= 4)
        return int(requested - 1);
    if (requested)
        return -1;
    int smallest = -1;
    for (int i = 0; i < 4; ++i)
    {
        if (!sizes[i])
            return i;
        if (smallest < 0 || sizes[i] < sizes[smallest])
            smallest = i;
    }
    return smallest >= 0 && sizes[smallest] < newSize ? smallest : -1;
}

// Where the items of the bag being replaced go: for each item, the first free slot (backpack and the other bags) that
// takes it (a herb bag takes herbs only) and is not taken yet. Empty when one of them has nowhere to go: then nothing
// moves, and the bag stays.
inline std::vector<size_t> AssignSlots(size_t items, size_t slots, std::function<bool(size_t, size_t)> const& accepts)
{
    std::vector<size_t> to;
    std::vector<bool> taken(slots, false);
    for (size_t i = 0; i < items; ++i)
    {
        size_t s = 0;
        while (s < slots && (taken[s] || !accepts(i, s)))
            ++s;
        if (s == slots)
            return {};
        taken[s] = true;
        to.push_back(s);
    }
    return to;
}

// A profession tool or material, never listed: trade goods, reagents, keys and gems (ore, leather, herbs, cloth, runed
// rods), a fishing pole (a weapon, subclass 20) and any item that serves as a tool (TotemCategory: picks, knives,
// hammers, spanners, rods). A white Fishing Pole was offered to "destroy" (3.0.1 in game).
inline bool IsProfessionItem(uint32_t itemClass, uint32_t subClass, uint32_t totemCategory)
{
    constexpr uint32_t WEAPON = 2, REAGENT = 5, TRADE_GOODS = 7, GEM = 3, KEY = 13, FISHING_POLE = 20;
    return itemClass == TRADE_GOODS || itemClass == REAGENT || itemClass == KEY || itemClass == GEM ||
           (itemClass == WEAPON && subClass == FISHING_POLE) || totemCategory != 0;
}

// "alt bagcheck": why an item the player picked may not be sold or destroyed ("ok": it may), whatever its quality.
// The same rules as the lists (Eligible), in this order.
inline char const* Refusal(Candidate const& c, bool forSale)
{
    if (c.quest)
        return "quest";
    if (c.tool)
        return "tool";
    if (c.upgrade)
        return "wear";
    if (c.useSpell)
        return "use";
    if (c.kept)
        return "kept";
    if (c.outfit)
        return "outfit";
    if (c.container)
        return "container";
    if (c.ammo)
        return "ammo";
    if (forSale && c.conjured)
        return "conjured";
    if (forSale && c.noPrice)
        return "noprice";
    return "ok";
}

constexpr size_t BAGCHECK_MAX = 40;

inline bool Eligible(Candidate const& c, uint32_t maxQuality, bool forSale)
{
    if (maxQuality > MAX_QUALITY || c.quality > maxQuality)
        return false;
    if (c.quest || c.useSpell || c.upgrade || c.kept || c.outfit || c.container || c.ammo || c.tool)
        return false;
    if (forSale && (c.conjured || c.noPrice))
        return false;
    return true;
}
}  // namespace CoaAltBags

#endif
