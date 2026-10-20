/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AQ20UTILS_H
#define PLAYERBOTS_AQ20UTILS_H

#include "GameObject.h"
#include "Unit.h"

class RaidAq20Utils
{
public:
    static bool IsOssirianBuffActive(Unit* ossirian);
    static int32 GetOssirianDebuffTimeRemaining(Unit* ossirian);
    static GameObject* GetNearestCrystal(Unit* ossirian);
    // Conquest of Azeroth rebuilt the fight without crystals; the crystal logic above
    // only applies to the stock fight.
    static bool IsCoaOssirian(Unit* ossirian);
};

#endif
