// CoA 3.3.1: the bots have neither Inscription nor Jewelcrafting (CoaProfessionRules.h).
#include "CoaFormulaRules.h"
#include "CoaProfessionRules.h"

#include <cassert>
#include <cstdio>
#include <vector>

using namespace CoaProfessionRules;

struct Pair
{
    uint16_t firstSkill;
    uint16_t secondSkill;
    uint32_t weight;
};

int main()
{
    // Retired: Inscription and Jewelcrafting only.
    assert(Retired(773) && Retired(755));
    assert(!Retired(333) && !Retired(186) && !Retired(164) && !Retired(202) && !Retired(182) && !Retired(171));

    // The draw: every pair that names one goes, the others keep their weight. Mining stays with Blacksmithing and
    // Engineering; Herbalism with Alchemy.
    std::vector<Pair> pairs = {{186, 164, 45}, {186, 202, 30}, {186, 755, 15}, {182, 171, 10}, {182, 773, 25},
                               {773, 182, 5}};
    DropRetired(pairs);
    assert(pairs.size() == 3);
    for (Pair const& p : pairs)
        assert(!Retired(p.firstSkill) && !Retired(p.secondSkill));
    assert(pairs[0].secondSkill == 164 && pairs[1].secondSkill == 202 && pairs[2].secondSkill == 171);
    assert(pairs[0].weight == 45);

    // Formulas: a recipe of a retired profession is never learnable (the bot's profession check fails).
    uint32_t const profession = 755;
    bool const hasSkill = true;  // a bot that still has it, before it changes
    assert(!CoaFormulaRules::Learnable(25278, false, !Retired(profession) && hasSkill, true, true));
    assert(CoaFormulaRules::Learnable(20034, false, !Retired(333) && hasSkill, true, true));

    // Items:
    //  products: glyph 43412 and cut gem 23095 go; a scroll the world loots (1180) and a ring Blacksmithing also
    //  makes (9999) stay;
    //  reagents: pigment 39151 and ink 37101 go; a raw gem mined (1210) and a bar Blacksmithing uses (2840) stay;
    //  recipe items: a Design sold only (20855) goes, a Design looted (20971) stays.
    std::vector<uint32_t> products = {43412, 23095, 1180, 9999, 37101};
    std::vector<uint32_t> reagents = {39151, 37101, 1210, 2840};
    std::vector<uint32_t> recipeItems = {20855, 20971};
    std::unordered_set<uint32_t> otherProducts = {9999};
    std::unordered_set<uint32_t> otherReagents = {2840};
    std::unordered_set<uint32_t> world = {1180, 1210, 20971};
    std::unordered_set<uint32_t> items = RetiredItems(products, reagents, recipeItems, otherProducts, otherReagents, world);
    for (uint32_t id : {43412u, 23095u, 39151u, 37101u, 20855u})
        assert(items.count(id));
    for (uint32_t id : {1180u, 9999u, 1210u, 2840u, 20971u})
        assert(!items.count(id));
    assert(items.size() == 5);

    // Pace: 6 a minute, a new minute starts again, a clock behind restarts the window.
    Throttle t;
    for (int i = 0; i < 6; ++i)
        assert(t.Take(1000, 60, 6));
    assert(!t.Take(1030, 60, 6));
    assert(t.Take(1060, 60, 6));
    assert(t.Take(500, 60, 6));
    assert(!Throttle().Take(1000, 60, 0));

    std::puts("coa_profession_rules: OK");
    return 0;
}
