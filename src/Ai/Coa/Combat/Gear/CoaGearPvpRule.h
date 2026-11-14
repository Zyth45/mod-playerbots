/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COAGEARPVPRULE_H
#define _PLAYERBOT_COAGEARPVPRULE_H

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// Which PvP pieces a player can get, from the world data alone. Pure, so
// tests/coa_combat_gear_pvp.cpp reads it as is; CoaGearPvp.cpp feeds it the item templates and the source tables.
namespace CoaGearPvpRule
{
    struct Item
    {
        uint32_t id = 0;
        std::string name;
        uint32_t inventoryType = 0;
        bool pvp = false;  // a weapon or armor with resilience or an honor rank required
    };

    // The prefixes a variant adds to its base's name.
    inline std::string StripVariantPrefixes(std::string name)
    {
        static char const* const prefixes[] = {"Bloodforged ", "Twisted ", "Replica ", "Ascended ", "Mythic ",
                                               "Heroic ", "Empowered ", "Infused "};
        for (bool changed = true; changed;)
        {
            changed = false;
            for (char const* p : prefixes)
            {
                std::string const prefix(p);
                if (name.compare(0, prefix.size(), prefix) == 0)
                {
                    name.erase(0, prefix.size());
                    changed = true;
                }
            }
        }
        return name;
    }

    // Pieces given by a script, found in no table, kept on purpose. By name, so their variants follow.
    inline bool IsWhitelisted(std::string const& name)
    {
        // Prefixes: Battlemaster's (11061, 11063 and their variants), Adversary's amulets (1500533-1500536), the
        // Bloody Champion's / Bloody Lieutenant Commander's series (79xxxxx).
        static char const* const prefixes[] = {"Battlemaster's ", "Adversary's ", "Bloody Champion's ",
                                               "Bloody Lieutenant Commander's "};
        // Single pieces with no table source in the study: Glacierbound Wristguards (18084), Timbermaw Shanker
        // (1519891), Outrider's Cloth Gloves (305007), Darkbane Staff (202210).
        static char const* const names[] = {"Glacierbound Wristguards", "Timbermaw Shanker", "Outrider's Cloth Gloves",
                                            "Darkbane Staff"};
        std::string const base = StripVariantPrefixes(name);
        for (char const* p : prefixes)
            if (base.compare(0, std::string(p).size(), p) == 0)
                return true;
        for (char const* n : names)
            if (base == n)
                return true;
        return false;
    }

    // A gossip option: OptionType 3 opens the vendor list ActionMenuID (the creature's own list when 0), OptionType 1
    // opens the menu ActionMenuID.
    struct GossipOption
    {
        uint32_t menu = 0;
        uint32_t type = 0;
        uint32_t actionMenu = 0;
    };

    // The vendor lists a player can open: a spawned creature's own list, the lists its gossip menus open (through
    // sub-menus too), and the lists those lists reference (npc_vendor rows with a negative item).
    // `creatureMenus`: (creature entry, gossip_menu_id). `references`: (vendor list, referenced list).
    inline std::unordered_set<uint32_t> ReachableVendorLists(
        std::unordered_set<uint32_t> const& spawnedCreatures,
        std::vector<std::pair<uint32_t, uint32_t>> const& creatureMenus, std::vector<GossipOption> const& options,
        std::vector<std::pair<uint32_t, uint32_t>> const& references)
    {
        std::unordered_set<uint32_t> lists = spawnedCreatures;
        std::unordered_set<uint32_t> menus;
        std::vector<uint32_t> pending;
        for (auto const& [entry, menu] : creatureMenus)
            if (menu && spawnedCreatures.count(entry) && menus.insert(menu).second)
                pending.push_back(menu);
        std::unordered_map<uint32_t, std::vector<GossipOption const*>> byMenu;
        for (GossipOption const& option : options)
            byMenu[option.menu].push_back(&option);
        while (!pending.empty())
        {
            uint32_t const menu = pending.back();
            pending.pop_back();
            auto it = byMenu.find(menu);
            if (it == byMenu.end())
                continue;
            for (GossipOption const* option : it->second)
            {
                if (!option->actionMenu)
                    continue;
                if (option->type == 3)
                    lists.insert(option->actionMenu);
                else if (option->type == 1 && menus.insert(option->actionMenu).second)
                    pending.push_back(option->actionMenu);
            }
        }
        for (bool changed = true; changed;)
        {
            changed = false;
            for (auto const& [from, to] : references)
                if (lists.count(from) && lists.insert(to).second)
                    changed = true;
        }
        return lists;
    }

    // The id offsets CoA gives its variants (6000000 for most, then 6300000, 3000000, 200000...).
    inline std::vector<uint32_t> BaseIdCandidates(uint32_t id)
    {
        std::vector<uint32_t> out{id % 100000u, id % 1000000u};
        for (uint32_t off : {6000000u, 6100000u, 6200000u, 6300000u, 800000u})
            if (id > off)
                out.push_back(id - off);
        return out;
    }

    // `sources`: every item a reachable vendor list (ReachableVendorLists), quest, loot table, cache, reward table or catalogue gives (the Manastorm demon's
    // random pool excluded). `derived`: (from, to) pairs, `to` obtainable when `from` is (heroic and mythic
    // dungeon drops, Worldforged upgrades). Returns the PvP pieces nobody can get.
    inline std::unordered_set<uint32_t> Unobtainable(std::vector<Item> const& items, std::unordered_set<uint32_t> sources,
                                                     std::vector<std::pair<uint32_t, uint32_t>> const& derived)
    {
        for (bool changed = true; changed;)
        {
            changed = false;
            for (auto const& [from, to] : derived)
                if (to && sources.count(from) && sources.insert(to).second)
                    changed = true;
        }

        std::unordered_map<uint32_t, Item const*> byId;
        std::map<std::pair<std::string, uint32_t>, std::vector<uint32_t>> byName;  // (name, inventory type) -> ids
        for (Item const& item : items)
        {
            byId[item.id] = &item;
            byName[std::make_pair(item.name, item.inventoryType)].push_back(item.id);
        }
        for (auto& [key, ids] : byName)
            std::sort(ids.begin(), ids.end());

        auto baseOf = [&](Item const& item) -> uint32_t
        {
            std::string const stripped = StripVariantPrefixes(item.name);
            for (uint32_t cand : BaseIdCandidates(item.id))
            {
                auto it = byId.find(cand);
                if (cand && cand != item.id && it != byId.end() && StripVariantPrefixes(it->second->name) == stripped)
                    return cand;
            }
            // Else the lowest other id carrying the stripped name in the same slot.
            auto it = byName.find(std::make_pair(stripped, item.inventoryType));
            if (it != byName.end())
                for (uint32_t cand : it->second)
                    if (cand != item.id)
                        return cand;
            return 0u;
        };

        // A name (variant prefixes stripped) and slot that some obtainable piece carries: its copies and variants are
        // the same piece for a player who looks for it.
        std::set<std::pair<std::string, uint32_t>> obtainableNames;
        for (Item const& item : items)
            if (sources.count(item.id))
                obtainableNames.emplace(StripVariantPrefixes(item.name), item.inventoryType);

        std::unordered_set<uint32_t> out;
        for (Item const& item : items)
        {
            if (!item.pvp || sources.count(item.id) || IsWhitelisted(item.name) ||
                obtainableNames.count(std::make_pair(StripVariantPrefixes(item.name), item.inventoryType)))
                continue;
            // A variant follows its base, whatever its quality.
            bool obtainable = false;
            std::unordered_set<uint32_t> seen{item.id};
            for (uint32_t base = baseOf(item); base && seen.insert(base).second;)
            {
                if (sources.count(base))
                {
                    obtainable = true;
                    break;
                }
                auto it = byId.find(base);
                if (it == byId.end())
                    break;
                base = baseOf(*it->second);
            }
            if (!obtainable)
                out.insert(item.id);
        }
        return out;
    }
}

#endif
