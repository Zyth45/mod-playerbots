/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COASTARTERWEAPONRULES_H
#define PLAYERBOTS_COASTARTERWEAPONRULES_H

// CoA 3.1.6 (AiPlayerbot.CoaSelfFound.StarterWeapon): on a self-found realm, a random bot left with no weapon at all
// (none worn, none in its bags; a fishing pole is no weapon) gets the starting weapon of its class, once in its life
// (table playerbots_coa_starter_weapon). Nothing else is given. Pure rule.
namespace CoaStarterWeaponRules
{
struct View
{
    bool enabled = false;      // AiPlayerbot.CoaSelfFound.StarterWeapon
    bool selfFoundRealm = false;  // AiPlayerbot.CoaSelfFound = 1
    bool randomBot = false;
    bool hasWeapon = false;    // worn or in the bags, a fishing pole left out
    bool givenBefore = false;  // already got it once (sold or destroyed since: never again)
};

inline bool Gives(View const& v)
{
    return v.enabled && v.selfFoundRealm && v.randomBot && !v.hasWeapon && !v.givenBefore;
}
}  // namespace CoaStarterWeaponRules

#endif
