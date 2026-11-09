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

    // What a bot may list: no ammunition (Doomshot, quivers), no vendor good, no later expansion item
    // (Netherweave Bandage 21990: required level 58, item level 64), a consumable only when crafted.
    assert(IsAmmo(6) && IsAmmo(11) && !IsAmmo(2) && !IsAmmo(0));
    assert(!BotMayList(6, false, true, false));    // Doomshot, from a vendor
    assert(!BotMayList(6, true, false, false));    // even crafted ammunition
    assert(!BotMayList(11, false, false, false));  // a quiver
    assert(!BotMayList(0, false, false, false));   // a looted potion: the bot keeps it
    assert(BotMayList(0, true, false, false));     // a potion its artisan made
    assert(!BotMayList(7, false, true, false));    // a vendor reagent, resold
    assert(!BotMayList(0, true, false, true));     // Netherweave Bandage, crafted
    assert(BotMayList(4, false, false, false));    // looted armor
    assert(BotMayList(7, false, false, false));    // looted cloth
    assert(IsLaterExpansion(21990, 58, 64, false));
    assert(!IsLaterExpansion(21990, 58, 64, true));
    assert(!IsLaterExpansion(20000, 60, 70, false)); // out of the later expansions' id range
    assert(!IsLaterExpansion(21500, 50, 50, false)); // low requirements: kept

    // Clickable links: a profession rank (spell) and a recipe (trade skill).
    assert(SpellChatLink(3910, "Tailoring", false) == "|cff71d5ff|Hspell:3910|h[Tailoring]|h|r");
    assert(SpellChatLink(3914, "Brown Linen Vest", true) == "|cffffd000|Henchant:3914|h[Brown Linen Vest]|h|r");

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
