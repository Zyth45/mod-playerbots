/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COAPROFESSIONTOOLRULES_H
#define _PLAYERBOT_COAPROFESSIONTOOLRULES_H

#include <cstdint>

// CoA 3.3.1: a profession tool is never a bot's weapon. In the CoA data the Skinning Knife (7005) is a dagger and the
// Mining Pick (2901) a main-hand "weapon", both of artifact quality (6, a score multiplier of 1.0 against 1.1 for a
// white item): the knife, item level 4 with 1-3 damage, outscored the grey starting weapons of item level 3 and was
// worn by hundreds of bots of levels 1-10, even over a Fire Poker (4.7 DPS). The real weapons that serve as a tool too
// (Finkle's Skinner, the Digmaster 5000, Brann's Trusty Pick: green and better, a totem category of their own) stay
// weapons.
namespace CoaProfessionToolRules
{
constexpr uint32_t ITEM_CLASS_WEAPON_ID = 2;
constexpr uint32_t WEAPON_SUBCLASS_MISC = 14;
constexpr uint32_t QUALITY_ARTIFACT = 6;

// A tool: a weapon a profession asks for (a totem category), of artifact quality or a miscellaneous one.
inline bool IsTool(uint32_t itemClass, uint32_t subClass, uint32_t quality, uint32_t totemCategory)
{
    return itemClass == ITEM_CLASS_WEAPON_ID && totemCategory != 0 &&
           (quality == QUALITY_ARTIFACT || subClass == WEAPON_SUBCLASS_MISC);
}

// A tool is worn only in an empty hand, the last resort of a bot with nothing else; a real weapon then takes its
// place (a worn tool scores nothing). Never a bare hand for want of a weapon.
inline bool MayWear(bool tool, bool handEmpty) { return !tool || handEmpty; }

// The weapon taken back after fishing: a real weapon over a tool, then the higher item level.
inline bool Prefer(bool newTool, uint32_t newLevel, bool hasHeld, bool heldTool, uint32_t heldLevel)
{
    if (!hasHeld)
        return true;
    if (heldTool != newTool)
        return heldTool;
    return newLevel > heldLevel;
}

// A tool already in the main hand (3.3.1 bench C2: 333 bots kept the Skinning Knife three hours, 220 of them with
// their only real weapon, a dagger, in the off hand, which the upgrade check never moves): a real weapon of the bags
// takes its place, else the off-hand weapon moves over; with neither, the tool stays (never a bare hand).
enum class Replace
{
    None,
    BagWeapon,
    OffHand
};

inline Replace ToolInHand(bool toolInMainHand, bool realWeaponInBags, bool offHandFitsMainHand, bool bagSpaceForTool)
{
    if (!toolInMainHand)
        return Replace::None;
    if (realWeaponInBags)
        return Replace::BagWeapon;  // the tool takes the weapon's place in the bags
    if (offHandFitsMainHand && bagSpaceForTool)
        return Replace::OffHand;
    return Replace::None;
}
}  // namespace CoaProfessionToolRules

#endif
