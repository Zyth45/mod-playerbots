/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAFORMULARULES_H
#define PLAYERBOTS_COAFORMULARULES_H

#include <cstddef>
#include <cstdint>

// CoA 3.3.1 ("super enchanters"): how a bot comes to know the recipes the world gives (a formula, a pattern, plans...).
// Pure rules, no game types.
namespace CoaFormulaRules
{
// The game's generic "learn" casts on a recipe item (the taught spell is then the item's other spell).
constexpr uint32_t GenericLearn = 483;
constexpr uint32_t GenericLearnWotlk = 55884;

// The spell a recipe item teaches. Two formats in the CoA data: a generic learn cast plus the taught spell (Plans:
// Lionheart Helm: 483, 16729), or one on-use learner whose LEARN_SPELL effect gives the taught spell (Formula: Enchant
// Weapon - Crusader: 113065 -> 20034). learnTrigger(id) returns the spell a learner teaches, 0 when it teaches none.
template <class LearnTrigger>
uint32_t TaughtSpell(uint32_t const* spellIds, std::size_t count, LearnTrigger learnTrigger)
{
    for (std::size_t i = 0; i < count; ++i)
    {
        uint32_t const id = spellIds[i];
        if (!id || id == GenericLearn || id == GenericLearnWotlk)
            continue;
        uint32_t const taught = learnTrigger(id);
        return taught ? taught : id;
    }
    return 0;
}

// A recipe the bot may learn now: one it does not know, of a profession it has, at its rank and level, and with the
// reputation the recipe asks for.
inline bool Learnable(uint32_t taughtSpell, bool known, bool hasProfession, bool canUse, bool reputationOk)
{
    return taughtSpell && !known && hasProfession && canUse && reputationOk;
}

// (d) The daily rare formula: an enchanter of minSkill or more, once per day, with a chance in percent; the roll is a
// value in [0, 100).
inline bool DrawsFormula(uint32_t skill, uint32_t minSkill, uint32_t roll, uint32_t percent)
{
    return skill >= minSkill && roll < percent;
}

// (b) What one formula may cost a bot: a share of its gold (the rest stays) and no more than its level allows.
inline uint64_t BuyCap(uint64_t money, uint32_t moneyPercent, uint32_t level, uint64_t copperPerLevel)
{
    uint32_t const percent = moneyPercent < 100 ? moneyPercent : 100;
    uint64_t const share = money * percent / 100;
    uint64_t const byLevel = uint64_t(level) * copperPerLevel;
    return share < byLevel ? share : byLevel;
}

// (b) Players first: a formula a player put up is bought by a bot only once the bots have seen it listed for delay
// seconds (players get the first day); a bot's listing at once.
inline bool ListingOpenToBots(bool sellerIsBot, uint32_t firstSeen, uint32_t now, uint32_t delay)
{
    return sellerIsBot || (now >= firstSeen && now - firstSeen >= delay);
}
}  // namespace CoaFormulaRules

#endif
