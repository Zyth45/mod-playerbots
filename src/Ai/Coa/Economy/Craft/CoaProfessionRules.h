/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAPROFESSIONRULES_H
#define PLAYERBOTS_COAPROFESSIONRULES_H

#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <vector>

// CoA 3.3.1: the bots have neither Inscription nor Jewelcrafting. Conquest of Azeroth's live realm has no Inscription,
// and our core gives Jewelcrafting only in maps a classic realm keeps shut. A rule of the bots only: the players and
// the core are untouched. Pure rules, tested apart from the server.
namespace CoaProfessionRules
{
constexpr uint32_t Inscription = 773;
constexpr uint32_t Jewelcrafting = 755;

// A profession the bots no longer have.
inline bool Retired(uint32_t skill)
{
    return skill == Inscription || skill == Jewelcrafting;
}

// The draw's pairs without a retired profession: a pair that names one goes whole, so its weight falls to the others.
template <class Pair>
void DropRetired(std::vector<Pair>& pairs)
{
    pairs.erase(std::remove_if(pairs.begin(), pairs.end(),
                               [](Pair const& p) { return Retired(p.firstSkill) || Retired(p.secondSkill); }),
                pairs.end());
}

// The items only the retired professions give:
//  - what their recipes make, unless another profession makes it too or the world gives it (a scroll looted in the
//    world stays);
//  - the reagents only they use, unless another profession makes it or the world gives it (raw gems mined stay;
//    pigments milled and inks go);
//  - their recipe items (Design, Technique), unless the world gives them.
// "The world gives" = a loot of the world other than milling and prospecting, which are the retired professions' own.
inline std::unordered_set<uint32_t> RetiredItems(std::vector<uint32_t> const& retiredProducts,
                                                 std::vector<uint32_t> const& retiredReagents,
                                                 std::vector<uint32_t> const& retiredRecipeItems,
                                                 std::unordered_set<uint32_t> const& otherProducts,
                                                 std::unordered_set<uint32_t> const& otherReagents,
                                                 std::unordered_set<uint32_t> const& worldItems)
{
    std::unordered_set<uint32_t> out;
    for (uint32_t id : retiredProducts)
        if (id && !otherProducts.count(id) && !worldItems.count(id))
            out.insert(id);
    for (uint32_t id : retiredReagents)
        if (id && !otherReagents.count(id) && !otherProducts.count(id) && !worldItems.count(id))
            out.insert(id);
    for (uint32_t id : retiredRecipeItems)
        if (id && !worldItems.count(id))
            out.insert(id);
    return out;
}

// At most perWindow bots changed in a window of windowSeconds: 650 bots on a showcase do not all change at once.
struct Throttle
{
    uint32_t windowStart = 0;
    uint32_t used = 0;

    bool Take(uint32_t now, uint32_t windowSeconds, uint32_t perWindow)
    {
        if (!perWindow)
            return false;
        if (now >= windowStart + windowSeconds || now < windowStart)
        {
            windowStart = now;
            used = 0;
        }
        if (used >= perWindow)
            return false;
        ++used;
        return true;
    }
};
}  // namespace CoaProfessionRules

#endif
