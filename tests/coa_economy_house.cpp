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
    }
    // Bots' auctions per item and house: an upgrade kit at most 3 ("2-3 OK, not 50"), anything else at most 10.
    assert(IsUpgradeKit(0, 6) && !IsUpgradeKit(0, 5) && !IsUpgradeKit(7, 6) && !IsUpgradeKit(4, 6));
    assert(ItemCap(0, 6, 10, 3) == 3 && ItemCap(7, 5, 10, 3) == 10 && ItemCap(2, 7, 10, 3) == 10);
    assert(UnderItemCap(0, 3) && UnderItemCap(2, 3) && !UnderItemCap(3, 3) && !UnderItemCap(50, 3));
    assert(UnderItemCap(9, 10) && !UnderItemCap(10, 10));
    assert(UnderItemCap(5000, 0));                       // 0 = no cap
    return 0;
}
