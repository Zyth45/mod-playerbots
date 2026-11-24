/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAENCHANTSELFRULES_H
#define PLAYERBOTS_COAENCHANTSELFRULES_H

#include <cstdint>

// CoA 3.2 (AiPlayerbot.CoaEconomy.EnchantForSelf.Enable, the user's go of 21/11): an enchanter bot enchants its own
// gear with an enchantment it knows. Pure rules, no game types.
namespace CoaEnchantSelfRules
{
// An enchantment only goes on when it is worth more than the one there (never a better one replaced, never the same).
inline bool Replaces(float newScore, float currentScore) { return newScore > currentScore + 0.01f; }

// The enchantment's own limits: the wearer's level and, when the enchantment asks for one, a skill rank.
inline bool Usable(uint32_t botLevel, uint32_t requiredLevel, uint32_t requiredSkill, uint32_t botSkillValue,
                   uint32_t requiredSkillValue)
{
    if (requiredLevel && botLevel < requiredLevel)
        return false;
    return !requiredSkill || botSkillValue >= requiredSkillValue;
}
}  // namespace CoaEnchantSelfRules

#endif
