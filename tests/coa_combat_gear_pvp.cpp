// The PvP pieces a player can get (src/Ai/Coa/Combat/Gear/CoaGearPvpRule.h).
#include "Ai/Coa/Combat/Gear/CoaGearPvpRule.h"

#include <cassert>

using namespace CoaGearPvpRule;

int main()
{
    assert(StripVariantPrefixes("Bloodforged Twisted Marshal's Satin Pants") == "Marshal's Satin Pants");
    assert(IsWhitelisted("Bloodforged Battlemaster's Wrath"));
    assert(IsWhitelisted("Adversary's Amulet of Conquest"));
    assert(!IsWhitelisted("Grand Marshal's Kodohide Hauberk"));

    std::vector<Item> items = {
        {861899, "Corporal's Kodohide Hauberk", 20, true},       // sold
        {860108, "Grand Marshal's Kodohide Hauberk", 20, true},  // no source (the Manastorm pool is not one)
        {6416597, "Grand Marshal's Kodohide Hauberk", 20, true},
        {17603, "Marshal's Satin Pants", 7, true},               // sold
        {3332159, "Marshal's Satin Pants", 7, true},             // variant, same name: follows its base
        {22753, "Sentinel's Lamellar Legguards", 7, false},      // base, sold, no resilience
        {6022753, "Bloodforged Sentinel's Lamellar Legguards", 7, true},  // id + 6000000: follows its base
        {20175, "Defiler's Lizardhide Shoulders", 3, true},      // sold, rare
        {254832, "Defiler's Lizardhide Shoulders", 3, true},     // epic variant: the base's rule
        {18084, "Glacierbound Wristguards", 9, false},
        {6318084, "Bloodforged Glacierbound Wristguards", 9, true},  // whitelisted (script)
        {11063, "Battlemaster's Wrath", 2, true},                // whitelisted (script)
        {1500536, "Lonely Amulet", 2, true},                     // no source, no base
        {1550720, "Heroic Drop", 5, true},                       // heroic variant of a looted item
        {720, "Normal Drop", 5, false},
        {500, "Ordinary Sword", 13, false},                      // not PvP: never listed
    };
    std::unordered_set<uint32_t> sources = {861899, 17603, 22753, 20175, 720};
    std::vector<std::pair<uint32_t, uint32_t>> derived = {{720, 1550720}};

    std::unordered_set<uint32_t> const out = Unobtainable(items, sources, derived);
    assert(out.count(860108) && out.count(6416597));  // Grand Marshal's: gone
    assert(out.count(1500536));
    assert(!out.count(861899) && !out.count(17603) && !out.count(3332159));
    assert(!out.count(6022753) && !out.count(254832));
    assert(!out.count(6318084) && !out.count(11063));
    assert(!out.count(1550720) && !out.count(500));
    assert(out.size() == 3);

    // Vendor lists a player can open. 49053 is spawned, its gossip menu 9949053 opens the list 9490533 (weapons sold
    // for honor) and a sub-menu 9949054 that opens 9490536; 9490536 references 9490537. 12794 sells the same weapons
    // but has no spawn; 500 is spawned with its own list; 600 is spawned but its menu only opens a sub-menu nobody
    // lists.
    std::unordered_set<uint32_t> const spawned = {49053, 500, 600};
    std::vector<std::pair<uint32_t, uint32_t>> const creatureMenus = {
        {49053, 9949053}, {12794, 9949099}, {600, 7000}};
    std::vector<GossipOption> const options = {
        {9949053, 3, 9490533}, {9949053, 1, 9949054}, {9949054, 3, 9490536},
        {9949099, 3, 9490599},  // the menu of a creature with no spawn
        {7000, 1, 7001}, {9949053, 3, 0}};
    std::vector<std::pair<uint32_t, uint32_t>> const references = {{9490536, 9490537}, {9490599, 9490598}};
    std::unordered_set<uint32_t> const lists = ReachableVendorLists(spawned, creatureMenus, options, references);
    assert(lists.count(9490533) && lists.count(9490536) && lists.count(9490537));
    assert(lists.count(500) && lists.count(49053));
    assert(!lists.count(12794) && !lists.count(9490599) && !lists.count(9490598));
    assert(!lists.count(9949054) && !lists.count(7001) && !lists.count(0));

    // High Warlord's weapons: the ilvl 90 piece is sold by a reachable list, its ilvl 78 copy of the same name follows
    // it. A Grand Marshal's piece only an unspawned vendor sells stays out.
    std::vector<Item> const weapons = {
        {23464, "High Warlord's Battle Mace", 21, true},
        {23459, "High Warlord's Battle Mace", 21, true},
        {16441, "Grand Marshal's Claymore", 17, true},
    };
    std::unordered_set<uint32_t> const sold = {23464};  // in 9490533; 16441 only in 12794's list
    std::unordered_set<uint32_t> const gone = Unobtainable(weapons, sold, {});
    assert(!gone.count(23464) && !gone.count(23459) && gone.count(16441));
    return 0;
}
