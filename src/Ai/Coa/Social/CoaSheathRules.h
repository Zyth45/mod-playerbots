/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COASHEATHRULES_H
#define PLAYERBOTS_COASHEATHRULES_H

#include <cstdint>

// The bots' weapons (#11610, AiPlayerbot.CoaLive.Sheath.Enable): a player's client draws and puts away the weapon,
// a bot has none, so it walked about the capitals sword in hand and fought with it on its back. Drawn in a fight
// (the ranged one for a ranged bot), put away out of it, on a mount and in flight. Pure rule.
namespace CoaSheath
{
// The values of SheathState in the core.
constexpr uint8_t UNARMED = 0;
constexpr uint8_t MELEE = 1;
constexpr uint8_t RANGED = 2;

inline uint8_t Wanted(bool inCombat, bool mounted, bool flying, bool ranged, bool hasRangedWeapon)
{
    if (mounted || flying || !inCombat)
        return UNARMED;
    return ranged && hasRangedWeapon ? RANGED : MELEE;
}
}  // namespace CoaSheath

#endif
