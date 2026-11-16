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

    // Guild stock: what a gatherer keeps, its kinds, the intermediates, the guild's share of the house.
    assert(GathererKeeps(7, 5, false, false, false) && !GathererKeeps(7, 7, false, true, true));
    assert(GathererKeeps(7, 7, true, false, false) && GathererKeeps(7, 9, false, true, false));
    assert(GathererKeeps(7, 6, false, false, true) && !GathererKeeps(7, 12, true, true, true) && !GathererKeeps(4, 5, true, true, true));
    assert((PickStockKinds({ { 2770, 10 }, { 2771, 20 }, { 2772, 30 }, { 3858, 40 } }, 24, 2) == std::vector<std::uint32_t>{ 2771, 2772 }));
    assert((PickStockKinds({ { 7, 25 }, { 3, 15 } }, 20, 3) == std::vector<std::uint32_t>{ 3, 7 }));
    assert(PickStockKinds({ { 1, 1 } }, 20, 0).empty());
    assert(IsIntermediate(197, 7, 5) && IsIntermediate(165, 7, 6) && IsIntermediate(186, 7, 7));
    assert(!IsIntermediate(197, 4, 5) && !IsIntermediate(164, 7, 7) && !IsIntermediate(197, 7, 6));
    // 3.1.4 bank: a gatherer banks all of its stock (2 stacks of 20); an artisan keeps one stack in the bags; nothing
    // beyond the stock; a full bank takes nothing more.
    assert(BankRoom(8, 0, 20, 2, 0) == 8);
    assert(BankRoom(50, 0, 20, 2, 0) == 40);
    assert(BankRoom(8, 0, 20, 2, 20) == 0);
    assert(BankRoom(30, 0, 20, 2, 20) == 10);
    assert(BankRoom(10, 40, 20, 2, 0) == 0);
    assert(BankRoom(5, 0, 20, 1, 40) == 0);
    // 3.1.4: held kinds first (most held first), then the closest to the level.
    {
        std::vector<StockCandidate> ore = {{10620, 50, 0}, {3858, 40, 12}, {2772, 30, 30}, {12359, 50, 0}};
        assert((PickHeldStockKinds(ore, 60, 2) == std::vector<std::uint32_t>{2772, 3858}));
        assert((PickHeldStockKinds(ore, 60, 3) == std::vector<std::uint32_t>{2772, 3858, 10620}));
        std::vector<StockCandidate> none = {{10620, 50, 0}, {3858, 40, 0}};
        assert((PickHeldStockKinds(none, 60, 1) == std::vector<std::uint32_t>{10620}));
    }
    // 3.1.4 refresh: guild stock off, nothing stays (3.1.3); on, a stock material stays up to Stacks + 1 stacks.
    for (std::uint32_t kept : {0u, 20u, 60u})
        assert(!RefreshKeepsStock(false, true, kept, 20, 20, 2));
    assert(!RefreshKeepsStock(true, false, 0, 1, 20, 2));
    {
        // 5 stacks of Runecloth in the bags, Stacks 2: 3 stay (60 items), 2 go.
        std::uint32_t kept = 0, stayed = 0;
        for (int n = 0; n < 5; ++n)
            if (RefreshKeepsStock(true, true, kept, 20, 20, 2))
            {
                kept += 20;
                ++stayed;
            }
        assert(stayed == 3 && kept == 60);
        // Partial stacks fill up to the bound: 15 + 15 + 15 + 15 = 60 stays, the fifth 15 goes.
        kept = 0;
        stayed = 0;
        for (int n = 0; n < 5; ++n)
            if (RefreshKeepsStock(true, true, kept, 15, 20, 2))
            {
                kept += 15;
                ++stayed;
            }
        assert(stayed == 4 && kept == 60);
        // Default Kinds 3 x (Stacks 2 + 1) = 9 full stacks at most.
        assert(3 * (60 / 20) == 9);
    }
    // 3.1.4 artisans and players: a player's lot 1 copper under the house's cheapest is taken, even over BuyBelowPercent;
    // over it, a bot's lot, no house auction, a buy-back within 24 h or the key off: no; 3 auctions are then enough.
    assert(ArtisanMatchesPlayer(true, true, 2176, 2177, false));
    assert(!ArtisanMatchesPlayer(true, true, 2178, 2177, false));
    assert(!ArtisanMatchesPlayer(true, false, 2176, 2177, false));
    assert(!ArtisanMatchesPlayer(true, true, 2176, 0, false));
    assert(!ArtisanMatchesPlayer(true, true, 2176, 2177, true));
    assert(!ArtisanMatchesPlayer(false, true, 2176, 2177, false));
    assert(StockMinListings(true, 8, 3) == 3 && StockMinListings(false, 8, 3) == 8);
    // 25 % of 3 auctions is under one lot: the guild's first lot of the item a day is free of the share, not the next.
    assert(!GuildMayBuy(3, 0, 0, StockMinListings(true, 8, 3), 25, 10));
    assert(GuildMayBuy(3, 0, 0, StockMinListings(true, 8, 3), 25, 10, true));
    assert(!GuildMayBuy(3, 1, 1, StockMinListings(true, 8, 3), 25, 10, true));
    assert(!GuildMayBuy(3, 0, 0, StockMinListings(false, 8, 3), 25, 10));
    assert(!GuildMayBuy(4, 0, 10, StockMinListings(true, 8, 3), 25, 10, true));  // the guild's 10 a day still hold
    assert(!GuildMayBuy(2, 0, 0, StockMinListings(true, 8, 3), 25, 10, true));   // and the 3 auctions
    assert(GuildMayBuy(8, 0, 0, 8, 25, 10) && GuildMayBuy(7, 1, 1, 6, 25, 10) && !GuildMayBuy(6, 2, 2, 6, 25, 10));
    assert(!GuildMayBuy(7, 0, 0, 8, 25, 10) && !GuildMayBuy(20, 0, 10, 8, 25, 10) && !GuildMayBuy(20, 0, 0, 8, 25, 0));
    assert(!GuildMayBuy(3, 0, 0, 1, 25, 10) && GuildMayBuy(4, 0, 0, 1, 25, 10));

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

    // A rebuilt bot: at 100 % the roll replaces its gold; below, it keeps what it earned.
    assert(RebuiltBotGold(9000000, 500000, 100) == 500000 && RebuiltBotGold(0, 500000, 100) == 500000);
    assert(RebuiltBotGold(9000000, 500000, 0) == 9000000 && RebuiltBotGold(0, 500000, 0) == 0);
    assert(RebuiltBotGold(50000, 500000, 20) == 100000 && RebuiltBotGold(300000, 500000, 20) == 300000);
    {
        // The 3.0.1 default: about 100 gold in all by level 60, 255 by 80.
        auto const table = ParseLevelGold("10:5000,15:10000,20:20000,25:30000,30:50000,35:70000,40:100000,45:120000,"
                                          "50:150000,55:200000,60:250000,65:300000,70:350000,75:400000,80:500000");
        uint64_t by60 = 0, by80 = 0;
        for (uint32_t l = 1; l < 80; ++l)
        {
            by80 += LevelGoldFor(table, l, l + 1);
            if (l < 60)
                by60 += LevelGoldFor(table, l, l + 1);
        }
        assert(table.size() == 15 && by60 == 1005000 && by80 == 2555000);
    }

    // Bots' level gold: parsed, bad entries skipped; paid on a one-level step only.
    {
        auto const table = ParseLevelGold("10:10000, 20:50000,x:5,1:9,30:abc,40:300000");
        assert(table.size() == 3 && table.at(10) == 10000 && table.at(20) == 50000 && table.at(40) == 300000);
        assert(LevelGoldFor(table, 9, 10) == 10000 && LevelGoldFor(table, 10, 11) == 0);
        assert(LevelGoldFor(table, 1, 10) == 0);   // set at level 10 at once: nothing
        assert(ParseLevelGold("").empty());
    }

    // Engineering upgrade kits (CoA data): "level 1, item level 4" products made of Outland and Northrend bars are of
    // a later expansion through their reagent; a Copper Bar recipe stays of the era.
    {
        auto later = [](std::uint32_t id, std::uint32_t req, std::uint32_t ilvl) { return IsLaterExpansion(id, req, ilvl, false); };
        bool const khoriumKit = later(135127, 1, 4), felIronKit = later(135130, 1, 4), saroniteKit = later(135131, 1, 4);
        assert(!khoriumKit && !felIronKit && !saroniteKit);                       // the products alone pass
        assert(RecipeIsLaterExpansion(khoriumKit, {later(23449, 58, 70)}));      // 9 Khorium Bar (spell 94381)
        assert(RecipeIsLaterExpansion(felIronKit, {later(23445, 58, 60)}));      // 5 Fel Iron Bar (spell 94387)
        assert(RecipeIsLaterExpansion(saroniteKit, {later(36913, 58, 80)}));     // 9 Saronite Bar (spell 94389)
        assert(!RecipeIsLaterExpansion(false, {later(2840, 1, 4)}));             // Copper Bar
        assert(!RecipeIsLaterExpansion(false, {}));
        assert(OnlyFromLaterRecipes({true, true}) && !OnlyFromLaterRecipes({true, false}) && !OnlyFromLaterRecipes({}));
    }

    // Reagents paid at delivery: only when on, short of gold and covered by the price.
    assert(MaterialsOnDelivery(true, 5000, 1000, 20000) == 5000);
    assert(MaterialsOnDelivery(false, 5000, 1000, 20000) == 0);
    assert(MaterialsOnDelivery(true, 5000, 5000, 20000) == 0);   // it has the gold: paid now
    assert(MaterialsOnDelivery(true, 20000, 0, 20000) == 0);     // the price does not cover them
    assert(MaterialsOnDelivery(true, 0, 0, 20000) == 0);

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
    // An artisan's quote: whispered to "can you make X?" (the deal waits on it), never on the addon's request (its card
    // shows the price; a whisper per item clicked came late, for an item never ordered).
    assert(QuoteWhispered(false) && !QuoteWhispered(true));

    // A whisper naming an item the artisan makes asks for its quote: a price, trade or craft word, or the link alone.
    {
        std::string const cloak = "|cff1eff00|hitem:14134:0:0:0:0:0:0:0:40|h[cloak of fire]|h|r";
        assert(StripItemLinks("how much for " + cloak + "?") == "how much for ?");
        assert(StripItemLinks("|hitem:2589:0|h[linen cloth]|h") == "");
        for (std::string const& ask : { "how much for " + cloak, "price for " + cloak, cloak, cloak + "?",
                                        "wts? " + cloak, "can you craft " + cloak, "do you have " + cloak,
                                        cloak + " pst", "combien pour " + cloak, "prix " + cloak + " ?",
                                        "tu le vends " + cloak, std::string("\xc3\xa7" "a co\xc3\xbbte ") + cloak })
            assert(AsksCraftQuote(ask));
        for (std::string const& other : { "look at my " + cloak, "where did you get " + cloak, "lol " + cloak,
                                          "nice " + cloak + " grats", std::string("hi") })
            assert(!AsksCraftQuote(other));
    }
    // Never cooking (185) nor first aid (129): "[X Bot] has earned the achievement [Lunch Lady]!" in bursts.
    assert(!RareRecipeProfession(185) && !RareRecipeProfession(129) && !RareRecipeProfession(0));
    assert(RareRecipeProfession(164) && RareRecipeProfession(333) && RareRecipeProfession(171) &&
           RareRecipeProfession(197) && RareRecipeProfession(165) && RareRecipeProfession(202));
    return 0;
}
