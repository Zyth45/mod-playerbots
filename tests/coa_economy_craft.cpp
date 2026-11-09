#include "CoaEconomyRules.h"

#include <cassert>
#include <unordered_map>

using namespace CoaEconomyRules;

int main()
{
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
    return 0;
}
