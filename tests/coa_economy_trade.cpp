#include "CoaEconomyRules.h"

#include <cassert>

using namespace CoaEconomyRules;

int main()
{
    // RpgSell: a vendor when the bags reach the threshold and something can be sold.
    assert(WantsVendor(true, 70, 70, true));
    assert(!WantsVendor(true, 69, 70, true));
    assert(!WantsVendor(true, 90, 70, false));
    assert(!WantsVendor(false, 100, 70, true));

    // Vendor goods always go; auction loot only without BotAuctions, or with bags 95 % full.
    assert(SellsAtVendor(true, false, true, 10));
    assert(SellsAtVendor(false, true, false, 10));
    assert(!SellsAtVendor(false, true, true, 94));
    assert(SellsAtVendor(false, true, true, 95));
    assert(!SellsAtVendor(false, false, false, 100));

    // A bot listing: 85 % (impatient) to 105 % (patient) of the reference, times the stack.
    assert(BotListingPrice(1000, 0.0f, 1) == 850);
    assert(BotListingPrice(1000, 1.0f, 1) == 1050);
    assert(BotListingPrice(1000, 1.0f, 20) == 21000);
    assert(BotListingPrice(0, 0.5f, 1) == 1);

    // Budget: 30 % of its gold, down to 9 % for the most thrifty.
    assert(BotBudget(100000, 30, 0.0f) == 30000);
    assert(BotBudget(100000, 30, 1.0f) == 9000);
    assert(BotBudget(0, 30, 0.0f) == 0);

    // Artisans: at least 20 % over the materials, and never a loss.
    assert(IsProfitable(1200, 1000, 20));
    assert(!IsProfitable(1199, 1000, 20));
    assert(!IsProfitable(1000, 1000, 0));
    assert(IsProfitable(100, 0, 20));

    // An order price: market value, never under materials + 15 %, x 0.95 .. 1.10 with patience.
    assert(CraftQuote(5000, 1000, 15, 0.0f) == 4750);
    assert(CraftQuote(1000, 1000, 15, 0.0f) == 1092);
    std::uint64_t const patientQuote = CraftQuote(1000, 1000, 15, 1.0f);  // 1150 x 1.10, floating point
    assert(patientQuote >= 1264 && patientQuote <= 1265);
    return 0;
}
