/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_GEAR_RULES_H
#define COA_GEAR_RULES_H

// Whether an item may enter the bots' gear caches (RandomItemMgr::IsValidItem), apart from the game objects, so that
// tests/coa_gear_rules.cpp can check it. Bloodforged copies (the core's Bloodforging::IsBloodforged) were handed to
// bots on realms where players cannot get them (Bloodforged.Enable = 0).
inline bool CoaGearAllowed(bool isBloodforged, bool bloodforgedEnabled)
{
    return !isBloodforged || bloodforgedEnabled;
}

#endif
