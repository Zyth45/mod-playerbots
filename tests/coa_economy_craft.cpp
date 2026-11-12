#include "CoaEconomyRules.h"

#include <cassert>
#include <unordered_map>

using namespace CoaEconomyRules;

int main()
{
    // Under half the real price: taken back at start-up.
    assert(CoaEconomyRules::UnderHalf(49, 100) && !CoaEconomyRules::UnderHalf(50, 100) && !CoaEconomyRules::UnderHalf(1, 0));
    assert(CoaEconomyRules::OutOfFairRange(49, 100) && CoaEconomyRules::OutOfFairRange(201, 100) &&
           !CoaEconomyRules::OutOfFairRange(200, 100) && !CoaEconomyRules::OutOfFairRange(50, 100) && !CoaEconomyRules::OutOfFairRange(9, 0));

    // Fair prices: a craft between 1.1 and 2 times its reagents, vendor bounds.
    assert(CoaEconomyRules::FairPrice(100000, 10000, 0, 0) == 20000);   // a scan far above its reagents
    assert(CoaEconomyRules::FairPrice(5000, 10000, 0, 0) == 11000);     // under its reagents
    assert(CoaEconomyRules::FairPrice(0, 10000, 0, 0) == 13000);        // not scanned: reagents + 30 %
    assert(CoaEconomyRules::FairPrice(15000, 10000, 0, 0) == 15000);
    assert(CoaEconomyRules::FairPrice(100, 0, 400, 0) == 500);          // never under a vendor's price + 25 %
    assert(CoaEconomyRules::FairPrice(900, 0, 10, 300) == 300);         // never over a vendor that sells it at will
    assert(CoaEconomyRules::FairPrice(0, 0, 400, 0) == 0);

    // Scanned prices kept within half and three times their band.
    assert(CoaEconomyRules::ClampToBand(45000000, 100000) == 300000);
    assert(CoaEconomyRules::ClampToBand(10000, 100000) == 50000);
    assert(CoaEconomyRules::ClampToBand(120000, 100000) == 120000);
    assert(CoaEconomyRules::ClampToBand(450000, 100000, 15) == 150000);
    // A rare at its top (1.5 x median x3) never above the epic step (1.5 x rare median x3).
    assert(CoaEconomyRules::ClampToBand(900000, 100000, 15) * 3 <= CoaEconomyRules::QualityStep(100000, 3.0f));
    assert(CoaEconomyRules::ClampToBand(120000, 0) == 120000 && CoaEconomyRules::ClampToBand(0, 5) == 0);
    // Factors green x2, blue x1.5: the dearest green (1.5 x median x2) never over the blue step, the
    // dearest blue (1.5 x median x1.5) never over the epic step.
    assert(CoaEconomyRules::ClampToBand(900000, 22300, 15) * 2 <= CoaEconomyRules::QualityStep(22300, 2.0f));
    assert(uint64_t(CoaEconomyRules::ClampToBand(900000, 80000, 15) * 1.5) <= CoaEconomyRules::QualityStep(80000, 1.5f));
    // Goods keep a cheap scan (no floor), lose only a joke price.
    assert(CoaEconomyRules::ClampToBand(517, 12000, 30, 0) == 517);
    assert(CoaEconomyRules::ClampToBand(99000, 12000, 30, 0) == 36000);

    // A better quality's least price: 1.5 x the scaled median of the quality below.
    assert(CoaEconomyRules::QualityStep(100000, 3.0f) == 450000 && CoaEconomyRules::QualityStep(0, 3.0f) == 0);

    // Median of prices (real house floors).
    assert(CoaEconomyRules::Median({}) == 0);
    assert(CoaEconomyRules::Median({5}) == 5);
    assert(CoaEconomyRules::Median({9, 1, 5}) == 5);
    assert(CoaEconomyRules::Median({4, 1, 3, 2}) == 2);

    // Silk Cloth 4111 -> Bolt of Silk Cloth 4305 (x4) -> a robe (2 bolts + 1 thread 2321).
    std::unordered_map<std::uint32_t, std::uint64_t> prices{{4111, 100}, {4305, 600}, {2321, 50}};
    std::unordered_map<std::uint32_t, CraftPath> paths;
    paths[4305] = CraftPath{1, {{4111, 4}}};
    paths[9000] = CraftPath{1, {{4305, 2}, {2321, 1}}};
    auto market = [&](std::uint32_t id) { auto it = prices.find(id); return it == prices.end() ? 0 : it->second; };
    auto pathOf = [&](std::uint32_t id) -> CraftPath const* { auto it = paths.find(id); return it == paths.end() ? nullptr : &it->second; };

    // A bolt: 4 cloth (400) beats the market (600).
    assert(RecursiveCost(4305, 3, market, pathOf) == 400);
    assert(RecursiveCost(4305, 0, market, pathOf) == 600);
    // The robe, unknown on the market: 2 x 400 + 50.
    assert(RecursiveCost(9000, 3, market, pathOf) == 850);
    // Depth 1: the bolts at their market price.
    assert(RecursiveCost(9000, 1, market, pathOf) == 1250);
    // Cheap bolts on the market win.
    prices[4305] = 300;
    assert(RecursiveCost(9000, 3, market, pathOf) == 650);
    // A reagent with no price and no recipe: the item keeps its market price, or 0.
    paths[9001] = CraftPath{1, {{7777, 1}}};
    assert(RecursiveCost(9001, 3, market, pathOf) == 0);
    prices[9001] = 42;
    assert(RecursiveCost(9001, 3, market, pathOf) == 42);
    // Several made per cast: rounded up per item.
    paths[9002] = CraftPath{3, {{4111, 1}}};
    assert(RecursiveCost(9002, 3, market, pathOf) == 34);
    // A loop (A from B, B from A) ends with the depth.
    paths[1] = CraftPath{1, {{2, 1}}};
    paths[2] = CraftPath{1, {{1, 1}}};
    prices[1] = 10;
    assert(RecursiveCost(1, 5, market, pathOf) == 10);

    // Breaking an item only when what comes out is worth more.
    assert(WorthBreaking(1000, 1001) && !WorthBreaking(1000, 1000));

    // Stock: up to 2 stacks of 20; a bargain at 90 % of the market or less.
    assert(KeepsInStock(40, 20, 2) && !KeepsInStock(41, 20, 2));
    assert(KeepsInStock(1, 0, 1));
    assert(StockBargain(90, 100, 90) && !StockBargain(91, 100, 90) && !StockBargain(1, 0, 90));

    // Auctions on game time: a night off moves every end by the night; a restart or an unknown past moves nothing.
    assert(AuctionPause(1000, 1000 + 8 * 3600, 300, 30 * 86400) == 8 * 3600);
    assert(AuctionPause(1000, 1200, 300, 30 * 86400) == 0);
    assert(AuctionPause(0, 5000, 300, 30 * 86400) == 0);
    assert(AuctionPause(5000, 1000, 300, 30 * 86400) == 0);
    assert(AuctionPause(1, 1 + 60 * 86400, 300, 30 * 86400) == 30 * 86400);

    // House-wide price scale: kept within 50-150, exact at 100, no overflow on large prices.
    assert(ClampPriceScale(10) == 50 && ClampPriceScale(100) == 100 && ClampPriceScale(400) == 150);
    assert(ScalePrice(12345, 100) == 12345 && ScalePrice(1000, 50) == 500 && ScalePrice(1000, 150) == 1500);
    assert(ScalePrice(199, 50) == 99 && ScalePrice(UINT64_MAX / 2, 150) > UINT64_MAX / 2);
    // Scaled down to half, a value still meets the vendor floor (FairPrice runs after the scale).
    assert(FairPrice(ScalePrice(100, 50), 0, 80, 0) == 100);

    // Bots' starting gold: unchanged at 100, none at 0, never above 100 %.
    assert(ScaleBotGold(500000, 100) == 500000 && ScaleBotGold(500000, 0) == 0 && ScaleBotGold(500000, 20) == 100000);
    assert(ScaleBotGold(500000, 400) == 500000 && ScaleBotGold(4000000000u, 50) == 2000000000u);

    // Bots' level gold: parsed, bad entries skipped; paid on a one-level step only.
    {
        auto const table = ParseLevelGold("10:10000, 20:50000,x:5,1:9,30:abc,40:300000");
        assert(table.size() == 3 && table.at(10) == 10000 && table.at(20) == 50000 && table.at(40) == 300000);
        assert(LevelGoldFor(table, 9, 10) == 10000 && LevelGoldFor(table, 10, 11) == 0);
        assert(LevelGoldFor(table, 1, 10) == 0);   // set at level 10 at once: nothing
        assert(ParseLevelGold("").empty());
    }

    // Rare recipes: the same answer for the same bot and recipe, never at 0 %, always at 100 %, near the percent.
    assert(RareRecipeDrawn(42, 16729, 15) == RareRecipeDrawn(42, 16729, 15));
    assert(!RareRecipeDrawn(42, 16729, 0) && RareRecipeDrawn(42, 16729, 100));
    {
        uint32_t drawn = 0;
        for (uint64_t bot = 1; bot <= 2000; ++bot)
            for (uint32_t spell = 16000; spell < 16010; ++spell)
                drawn += RareRecipeDrawn(bot, spell, 15) ? 1 : 0;
        assert(drawn > 2600 && drawn < 3400); // 15 % of 20000
    }
    assert(RareRecipeReachable(300, 300, 60, 55) && !RareRecipeReachable(299, 300, 60, 55));
    assert(!RareRecipeReachable(300, 300, 54, 55) && !RareRecipeReachable(0, 0, 60, 0));
    return 0;
}
