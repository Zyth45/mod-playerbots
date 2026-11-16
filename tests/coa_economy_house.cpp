#include "CoaEconomyRules.h"

#include <cassert>
#include <set>
#include <vector>

using namespace CoaEconomyRules;

int main()
{
    // Human buyer: a deal at its limit price keeps the base chance, a bargain is taken at once.
    assert(PurchaseChance(35, 1000, 1000) == 0.35);
    assert(PurchaseChance(35, 2000, 1000) == 1.0);
    assert(PurchaseChance(35, 1000, 2000) < 0.1);

    // Dearer goods take longer to be noticed.
    assert(NoticeDelayFactor(9999) == 1 && NoticeDelayFactor(10000) == 2);
    assert(NoticeDelayFactor(99999) == 2 && NoticeDelayFactor(100000) == 4);
    assert(NoticeDelayFactor(999999) == 4 && NoticeDelayFactor(1000000) == 8);

    // No arbitrage: the buyer never pays more than 90 % of the house lowest asking price.
    assert(ArbitrageCap(5000, 1000, 90) == 900);
    // 3.1.4 material buyer: 90 % of the house value; materials looked at first, then any candidate.
    assert(MaterialBuyerPrice(2770, 90) == 2493);
    assert(MaterialBuyerPrice(0, 90) == 0);
    assert(BuyerCandidate(3, 10, 7) == 1);
    assert(BuyerCandidate(0, 10, 7) == 7);
    assert(BuyerCandidate(5, 2, 3) == 1);
    assert(BuyerCandidate(0, 0, 3) == 0);
    // 3.1.4 BuyFromBots: off (default), a random bot's material is never bought; a player's is; on, both.
    assert(MaterialBuyerSkips(false, true) && !MaterialBuyerSkips(false, false));
    assert(!MaterialBuyerSkips(true, true) && !MaterialBuyerSkips(true, false));
    // 3.1.4 player matches the house: a player's Bronze Bar at 2176 under the house's 2177 sells at its price; a bot's,
    // or with the key off, or with no house auction, the usual cap.
    assert(PlayerMatchPay(true, true, 2177, 1500) == 2177);
    assert(MaterialBuyerTakes(2176 * 10, 10, PlayerMatchPay(true, true, 2177, 1500)));
    assert(!MaterialBuyerTakes(2178 * 10, 10, PlayerMatchPay(true, true, 2177, 1500)));
    assert(PlayerMatchPay(true, false, 2177, 1500) == 1500);
    assert(PlayerMatchPay(false, true, 2177, 1500) == 1500);
    assert(PlayerMatchPay(true, true, 0, 1500) == 1500);
    // The 24-hour rule: bought from the house at t, refused until t + 24 h; never bought, never refused.
    assert(BoughtFromHouseRecently(1000, 1000) && BoughtFromHouseRecently(1000, 1000 + 86399));
    assert(!BoughtFromHouseRecently(1000, 1000 + 86400) && !BoughtFromHouseRecently(0, 5000));
    // Per account and item: the same account's other item, or another account's same item, is not held.
    assert(HouseSaleKey(1, 2841) != HouseSaleKey(1, 2842) && HouseSaleKey(1, 2841) != HouseSaleKey(2, 2841));
    // The daily caps still hold for a player matched to the house: the 11th auction, or over 50 gold, refused.
    assert(!AccountMaySell(10, 0, 2176 * 10, 10, 500000) && !AccountMaySell(3, 490000, 21760, 10, 500000));
    // The loop it closes: buy the house's cheapest (0.85 V), list under the next (1.25 V): 0.34 V an item.
    assert(1250 * 95 / 100 - 850 == 337);
    // 3.1.4 fairness: looking at a material is free up to 40 a cycle; any other look uses one of the 3 candidates.
    // Account A's 17 refused stacks and 20 too dear: account B's 10 still looked at in the same cycle.
    {
        std::uint32_t looks = 0, slots = 0;
        for (int n = 0; n < 47; ++n)
            if (BuyerLookUsesSlot(true, ++looks))
                ++slots;
        assert(slots == 7 && !BuyerLookUsesSlot(true, 40) && BuyerLookUsesSlot(true, 41) && BuyerLookUsesSlot(false, 0));
    }

    // 3.1.4 guards of the material buyer.
    {
        // (a) The 28 floor materials an NPC vendor sells in CoA's world data (npc_vendor; vendor price in copper,
        // unlimited stock somewhere, items a day of all limited stocks, house value ~ CoA's real price). Unlimited:
        // never bought at a price that pays. Limited: the normal price unless a day of every vendor's stock would bring
        // over 10 gold (best reputation discount, house cut).
        struct Vendor { unsigned item; unsigned buyPrice; bool unlimited; std::uint64_t perDay; std::uint64_t house; };
        Vendor const vendors[] = {
            {765, 40, true, 152, 153}, {785, 80, true, 248, 483}, {2319, 200, false, 288, 2596},
            {2447, 40, true, 121, 294}, {2449, 80, true, 252, 4164}, {2453, 100, true, 195, 962},
            {2589, 55, false, 20, 1232}, {2592, 135, false, 10, 977}, {2770, 20, false, 10, 2033},
            {2771, 100, false, 6, 3191}, {2775, 300, false, 2, 12340}, {3355, 200, false, 117, 6415},
            {3356, 120, false, 99, 307}, {3357, 300, false, 72, 328}, {3358, 700, false, 6, 5117},
            {4234, 600, false, 288, 2751}, {4304, 1200, false, 384, 2403}, {4306, 600, false, 6, 1379},
            {4338, 1000, false, 4, 1577}, {8170, 2000, false, 288, 3876}, {8845, 1500, false, 6, 14278},
            {8846, 1000, false, 60, 10418}, {10938, 800, false, 1935, 538}, {10940, 800, false, 1093, 245},
            {13463, 400, false, 2, 5499}, {13464, 400, false, 4, 3727}, {13465, 600, false, 9, 10291},
            {13467, 1000, false, 6, 1701}};
        static_assert(sizeof(vendors) / sizeof(vendors[0]) == 28);
        std::set<unsigned> floor(MaterialFloorItems().begin(), MaterialFloorItems().end());
        std::set<unsigned> guarded;
        for (Vendor const& v : vendors)
        {
            assert(floor.count(v.item));
            for (std::uint64_t value : {v.house / 2, v.house, v.house * 2, 10000000ull})
            {
                std::uint64_t pay = MaterialBuyerPay(value, 90, 0, 90, 0);
                bool const guard = VendorGuardApplies(v.unlimited, v.perDay, pay, v.buyPrice);
                if (guard)
                    pay = VendorSafeCap(pay, v.buyPrice);
                if (v.unlimited)
                    assert(guard && pay * 95 < std::uint64_t(v.buyPrice) * 80);
                std::uint64_t const net = pay * 95 / 100;
                std::uint64_t const cost = std::uint64_t(v.buyPrice) * 80 / 100;
                assert(net <= cost || v.perDay * (net - cost) <= VendorAbuseCopperPerDay);
                if (value == v.house && guard)
                    guarded.insert(v.item);
            }
        }
        // At the house's value: the 5 unlimited ones, and 6 limited stocks worth over 10 gold a day (Medium, Heavy,
        // Thick and Rugged Leather, Wild Steelbloom, Gromsblood); Dreamfoil and the 16 others at the normal price.
        assert(guarded == (std::set<unsigned>{765, 785, 2447, 2449, 2453, 2319, 3355, 4234, 4304, 8170, 8846}));
        assert(!VendorGuardApplies(false, 2, 5499 * 90 / 100, 400));
        assert(!VendorGuardApplies(false, 0, 0, 0) && VendorGuardApplies(true, 0, 0, 40));
        // (c) No loop: what the house asks is at least its lowest price; the buyer pays at most 90 % of it, the seller
        // gets 95 % of that: buying the house's stack to sell it back loses at least 14 %.
        for (std::uint64_t lowest : {1ull, 100ull, 2571ull, 5877ull, 99999ull})
        {
            std::uint64_t const pay = MaterialBuyerPay(lowest * 2, 100, lowest, 90, 0);
            assert(pay <= lowest * 90 / 100 && pay * 95 / 100 < lowest);
        }
        assert(MaterialBuyerPay(2770, 90, 2571, 90, 0) == 2313);
        assert(MaterialBuyerPay(2770, 90, 0, 90, 0) == 2493);
        assert(MaterialBuyerPay(254, 90, 254, 90, 40) == 30);
        assert(VendorSafeCap(228, 40) == 30 && VendorSafeCap(228, 0) == 228);
        // (d) Its own buyout only, never above what it pays: 20 at 2300 within 2313 each, not 20 at 2314; no buyout, no.
        assert(MaterialBuyerTakes(46000, 20, 2313));
        assert(MaterialBuyerTakes(46260, 20, 2313));
        assert(!MaterialBuyerTakes(46280, 20, 2313));
        assert(!MaterialBuyerTakes(0, 20, 2313) && !MaterialBuyerTakes(100, 0, 2313) && !MaterialBuyerTakes(100, 1, 0));
        // (b) An account's day: 10 auctions, 50 gold (500000 copper); 0 = no cap.
        assert(AccountMaySell(9, 0, 1000, 10, 500000));
        assert(!AccountMaySell(10, 0, 1000, 10, 500000));
        assert(AccountMaySell(0, 490000, 10000, 10, 500000));
        assert(!AccountMaySell(0, 490000, 10001, 10, 500000));
        assert(AccountMaySell(1000, 99999999, 1, 0, 0));
    }
    assert(ArbitrageCap(500, 1000, 90) == 500);
    for (std::uint64_t lowest = 1; lowest < 100000; lowest += 997)
        assert(ArbitrageCap(lowest * 10, lowest, 90) < lowest);

    // Rarity: none from 5 % up, x2 at 0.01 % and below, a log scale in between.
    assert(RarityFactor(50.0, 1.0, 5.0, 0.01) == 1.0);
    assert(RarityFactor(5.0, 1.0, 5.0, 0.01) == 1.0);
    assert(RarityFactor(0.01, 1.0, 5.0, 0.01) == 2.0);
    assert(RarityFactor(0.001, 1.0, 5.0, 0.01) == 2.0);
    double const mid = RarityFactor(0.2236, 1.0, 5.0, 0.01);
    assert(mid > 1.49 && mid < 1.51);
    assert(RarityFactor(0.0, 1.0, 5.0, 0.01) == 1.0);

    // CoA filter, Bloodforged: always out while Bloodforged is off; with it on, out unless allowed; rule off = in.
    assert(LeavesOutBloodforged(true, false, true));
    assert(LeavesOutBloodforged(true, true, false));
    assert(!LeavesOutBloodforged(true, true, true));
    assert(!LeavesOutBloodforged(false, false, false));

    // The house seller counts its own auctions only (CoA: ~10,000 bots' auctions per faction kept it from listing).
    {
        std::vector<std::uint32_t> const owners = {3235, 5000, 5001, 3235, 7000, 3235};
        std::set<std::uint32_t> const sellers = {3235};
        assert(HouseSellerCount(owners, sellers) == 3);
        assert(HouseSellerCount(owners, std::set<std::uint32_t>{}) == 0);
        assert(HouseSellerCount(std::vector<std::uint32_t>{}, sellers) == 0);

        // Material floor: what is missing up to the floor, within the quota room; nothing once offered.
        assert(FloorStacksToList(0, 2, 100) == 2);
        assert(FloorStacksToList(1, 2, 100) == 1);
        assert(FloorStacksToList(2, 2, 100) == 0);
        assert(FloorStacksToList(7, 2, 100) == 0);
        assert(FloorStacksToList(0, 2, 1) == 1);
        assert(FloorStacksToList(0, 2, 0) == 0);
        // Never under another seller's lowest price per item; the market price when nobody else sells.
        assert(FloorUnitPrice(400, 0) == 400);
        assert(FloorUnitPrice(400, 350) == 400);
        assert(FloorUnitPrice(400, 900) == 900);
        // The faction houses only.
        assert(FloorServesHouse(2) && FloorServesHouse(6) && !FloorServesHouse(7) && !FloorServesHouse(1));
        // The list: Silver Bar and Linen Cloth in it, no duplicate, about ninety materials.
        {
            auto const& floor = MaterialFloorItems();
            std::set<std::uint32_t> const unique(floor.begin(), floor.end());
            assert(unique.size() == floor.size() && floor.size() >= 80 && floor.size() <= 100);
            assert(unique.count(2842) && unique.count(2589));
        }
    }
    // Bots' auctions per item and house: an upgrade kit at most 3 ("2-3 OK, not 50"), anything else at most 10.
    assert(IsUpgradeKit(0, 6) && !IsUpgradeKit(0, 5) && !IsUpgradeKit(7, 6) && !IsUpgradeKit(4, 6));
    assert(ItemCap(0, 6, 10, 3) == 3 && ItemCap(7, 5, 10, 3) == 10 && ItemCap(2, 7, 10, 3) == 10);
    assert(UnderItemCap(0, 3) && UnderItemCap(2, 3) && !UnderItemCap(3, 3) && !UnderItemCap(50, 3));
    assert(UnderItemCap(9, 10) && !UnderItemCap(10, 10));
    assert(UnderItemCap(5000, 0));                       // 0 = no cap
    // ah_sold (one per server per 30 min) and trade_window (one per player per 5 min): a paced line.
    assert(PaceAllows(0, 100, 1800));                    // never said yet
    assert(!PaceAllows(1000, 1000, 1800) && !PaceAllows(1000, 2799, 1800) && PaceAllows(1000, 2800, 1800));
    assert(!PaceAllows(5000, 5299, 300) && PaceAllows(5000, 5300, 300));
    assert(PaceAllows(5000, 5000, 0));                   // no gap
    return 0;
}
