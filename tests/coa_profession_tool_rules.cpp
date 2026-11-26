// CoA 3.3.1: a profession tool is never a bot's weapon (CoaProfessionToolRules.h), rows of the CoA item_template.
#include "CoaProfessionToolRules.h"

#include <cassert>

using CoaProfessionToolRules::IsTool;
using CoaProfessionToolRules::MayWear;
using CoaProfessionToolRules::Prefer;
using CoaProfessionToolRules::Replace;
using CoaProfessionToolRules::ToolInHand;

int main()
{
    assert(IsTool(2, 15, 6, 166));    // Skinning Knife 7005: a dagger of artifact quality
    assert(IsTool(2, 14, 6, 165));    // Mining Pick 2901
    assert(IsTool(2, 14, 6, 162));    // Blacksmith Hammer 5956
    assert(IsTool(2, 14, 1, 14));     // Arclight Spanner 6219: white, miscellaneous
    assert(IsTool(2, 14, 1, 166));    // Fletching Knife 818005
    assert(!IsTool(2, 15, 3, 166));   // Finkle's Skinner 12709: a real dagger
    assert(!IsTool(2, 0, 3, 165));    // Digmaster 5000 9465
    assert(!IsTool(2, 1, 2, 165));    // Cold Iron Pick 1959
    assert(!IsTool(2, 0, 1, 165));    // Kobold Excavation Pick 778: a white axe, a weapon
    assert(!IsTool(2, 15, 1, 0));     // Weathered Knife 484323: no totem category
    assert(!IsTool(2, 20, 1, 0));     // Fishing Pole 6256: CoaFishingGear's
    assert(!IsTool(4, 14, 6, 166));   // not a weapon

    // A bot with no real weapon still holds its tool; a tool never pushes anything out of a hand.
    assert(MayWear(true, true));
    assert(!MayWear(true, false));
    assert(MayWear(false, false) && MayWear(false, true));

    // After fishing: the Weathered Knife (item level 3) over the Skinning Knife (4); the tool when nothing else.
    assert(Prefer(false, 3, true, true, 4));
    assert(!Prefer(true, 4, true, false, 3));
    assert(Prefer(true, 4, false, false, 0));
    assert(Prefer(false, 8, true, false, 3) && !Prefer(false, 3, true, false, 8));
    assert(Prefer(true, 5, true, true, 4));

    // A tool already worn: dropped for a real weapon of the bags, or for the off-hand dagger; kept with neither.
    assert(ToolInHand(true, true, true, true) == Replace::BagWeapon);
    assert(ToolInHand(true, true, false, false) == Replace::BagWeapon);  // a swap: the bags need no room
    assert(ToolInHand(true, false, true, true) == Replace::OffHand);
    assert(ToolInHand(true, false, true, false) == Replace::None);      // full bags: the off hand would get the tool
    assert(ToolInHand(true, false, false, true) == Replace::None);      // nothing else: the tool stays in hand
    assert(ToolInHand(false, true, true, true) == Replace::None);
    return 0;
}
