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

// 3.2.2: the tools a spell asks for (a totem item or a totem category, such as a runed rod), as the game checks
// them for a player: every one asked for must be held.
inline bool HasTools(bool asks0, bool holds0, bool asks1, bool holds1)
{
    return (!asks0 || holds0) && (!asks1 || holds1);
}

// 3.2.2 (SelfStudy.RestAreas): where a bot studies: a capital, or, with the key on, any city or inn it rests in.
inline bool StudyPlace(bool capital, bool restAreas, bool restingInCity, bool restingInInn)
{
    return capital || (restAreas && (restingInCity || restingInInn));
}

// 3.2.2 (EnchantForSelf.Reagents): an enchanter keeps one stack (in its bags) of each reagent of the enchantments it
// knows; only the rest is sold, at a vendor or at the auction house.
inline bool KeepsReagent(uint32_t heldInBags, uint32_t stackSize)
{
    return heldInBags <= (stackSize ? stackSize : 1);
}

// 3.2.2: a green or better piece is disenchanted rather than sold: a bound piece it does not wear (one it replaced by
// a better one, bound loot: the item usage "disenchant", sellable only at a vendor) always; one it would sell (loot it cannot wear) when the dust is worth
// more; a green one of those also, whatever the prices, while the enchanter is short of its reagents (a blue or an
// epic piece is still sold then, unless the dust is worth more).
inline bool Disenchants(bool boundReplaced, bool worthMore, bool shortOfReagents, bool green)
{
    return boundReplaced || worthMore || (shortOfReagents && green);
}

// 3.2.2: why an enchanter put nothing on, one cause per try (the hourly count in playerbots.coa).
enum class Refusal : uint32_t
{
    None = 0,
    NoRecipe,  // no permanent enchantment it knows fits its level and skill
    NoRod,     // the enchantments it could put on ask for a tool it does not hold
    NoReagent, // reagents missing, and none to buy (no buying at this stop, or not for sale)
    NoGold,    // reagents for sale, but over its study's gold cap
    NoGain,    // it could put one on, but none beats what its pieces carry (or the gain is not worth the purchase)
    Count
};

// The cause told for a try that put nothing on: the furthest step reached by any enchantment it knows.
inline Refusal Cause(bool anyUsable, bool reachedPieces, bool goldShort, bool reagentShort, bool rodShort)
{
    if (!anyUsable)
        return Refusal::NoRecipe;
    if (reachedPieces)
        return Refusal::NoGain;
    if (goldShort)
        return Refusal::NoGold;
    if (reagentShort)
        return Refusal::NoReagent;
    return rodShort ? Refusal::NoRod : Refusal::NoRecipe;
}
}  // namespace CoaEnchantSelfRules

#endif
