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
    return 0;
}
