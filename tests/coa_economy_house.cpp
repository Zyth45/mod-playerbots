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

    // 3.1.4 guards of the material buyer.
    {
        // (a) The 28 floor materials an NPC vendor sells in CoA's world data (npc_vendor, any stock; vendor price in
        // copper). For any house value and house price, buying one at a vendor (20 % reputation discount at best) to
        // sell it to the house (5 % cut) always loses.
        struct Vendor { unsigned item; unsigned buyPrice; };
        Vendor const vendors[] = {
            {765, 40}, {785, 80}, {2319, 200}, {2447, 40}, {2449, 80}, {2453, 100}, {2589, 55}, {2592, 135},
            {2770, 20}, {2771, 100}, {2775, 300}, {3355, 200}, {3356, 120}, {3357, 300}, {3358, 700}, {4234, 600},
            {4304, 1200}, {4306, 600}, {4338, 1000}, {8170, 2000}, {8845, 1500}, {8846, 1000}, {10938, 800},
            {10940, 800}, {13463, 400}, {13464, 400}, {13465, 600}, {13467, 1000}};
        static_assert(sizeof(vendors) / sizeof(vendors[0]) == 28);
        std::set<unsigned> floor(MaterialFloorItems().begin(), MaterialFloorItems().end());
        for (Vendor const& v : vendors)
        {
            assert(floor.count(v.item));
            for (std::uint64_t value : {1ull, 40ull, 254ull, 2770ull, 58770ull, 10000000ull})
                for (std::uint64_t lowest : {0ull, 10ull, value, value * 3})
                {
                    std::uint64_t const pay = MaterialBuyerPay(value, 100, lowest, 100, v.buyPrice);
                    assert(pay * 95 < std::uint64_t(v.buyPrice) * 80);
                    assert(VendorSafeCap(value, v.buyPrice) * 95 < std::uint64_t(v.buyPrice) * 80);
                }
        }
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
