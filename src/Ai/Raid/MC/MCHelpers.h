/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_MCHELPERS_H
#define PLAYERBOTS_MCHELPERS_H

#include "Define.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "Unit.h"

#include <algorithm>
#include <cstddef>

// Stock and Conquest of Azeroth (mod-coa-raid-difficulty) ids side by side.
// CoA runs Molten Core on four difficulties. Every cast is resolved through
// SpellDifficulty.dbc before it is applied, so an aura carries the id of the
// current difficulty and every variant is listed.
namespace MoltenCoreHelpers
{
enum MoltenCoreNPCs
{
    // Golemagg
    NPC_CORE_RAGER = 11672,

    // Majordomo Executus
    NPC_MAJORDOMO_EXECUTUS = 12018,

    // Core Hound (trash)
    NPC_CORE_HOUND = 11671,
};

// CoA difficulty copies of a Molten Core creature: base + offset (01_mc_difficulty_templates.sql)
constexpr uint32 MC_ENTRY_OFFSET_HEROIC = 100000;
constexpr uint32 MC_ENTRY_OFFSET_MYTHIC = 200000;
constexpr uint32 MC_ENTRY_OFFSET_ASCENDED = 300000;

enum MoltenCoreSpells
{
    // Baron Geddon
    SPELL_INFERNO = 19695,                 // SpellDifficulty row 369: 19695/350084/350084/350084
    SPELL_INFERNO_DIFF_1_3 = 350084,
    SPELL_INFERNO_COA = 2105740,           // CoA: 3 s cast, then a 10 s aura on Geddon; no SpellDifficulty row
    SPELL_LIVING_BOMB = 20475,
    SPELL_LIVING_BOMB_COA_CAST = 2105701,  // CoA: 1 s dummy cast at the bomb's target
    SPELL_LIVING_BOMB_COA_D0 = 2105702,    // CoA: debuff applied when the dummy cast completes,
    SPELL_LIVING_BOMB_COA_D1 = 2105703,    // SpellDifficulty row 2115
    SPELL_LIVING_BOMB_COA_D2 = 2105704,
    SPELL_LIVING_BOMB_COA_D3 = 2105705,

    // Golemagg
    SPELL_GOLEMAGGS_TRUST = 20553,
    SPELL_MAGMA_SPLASH = 13880,            // SpellDifficulty row 295: 13880/13880/350099/350099
    SPELL_MAGMA_SPLASH_DIFF_2_3 = 350099,
};

// Aura on Baron Geddon while Inferno is active
constexpr uint32 INFERNO_AURAS[] = {SPELL_INFERNO, SPELL_INFERNO_DIFF_1_3, SPELL_INFERNO_COA};

// Debuff on the Living Bomb carrier
constexpr uint32 LIVING_BOMB_AURAS[] = {SPELL_LIVING_BOMB, SPELL_LIVING_BOMB_COA_D0, SPELL_LIVING_BOMB_COA_D1,
                                        SPELL_LIVING_BOMB_COA_D2, SPELL_LIVING_BOMB_COA_D3};

// Stacking debuff from striking Golemagg in melee
constexpr uint32 MAGMA_SPLASH_AURAS[] = {SPELL_MAGMA_SPLASH, SPELL_MAGMA_SPLASH_DIFF_2_3};

constexpr uint32 MAGMA_SPLASH_BACK_OFF_STACKS = 20;
constexpr float MAGMA_SPLASH_BACK_OFF_DISTANCE = 12.0f;

// Shazzrah's Arcane Explosion radius (stock 19712: 18 yd, CoA 2105601: 20 yd)
constexpr float ARCANE_EXPLOSION_DISTANCE = 26.0f;

template <std::size_t N>
inline bool HasAnyAura(Unit const* unit, uint32 const (&spellIds)[N])
{
    for (uint32 spellId : spellIds)
        if (unit->HasAura(spellId))
            return true;
    return false;
}

// A unit only carries the variant of its own difficulty, so the highest stack
// among the listed ids is that one.
template <std::size_t N>
inline uint32 GetAuraStacks(Unit const* unit, uint32 const (&spellIds)[N])
{
    uint32 stacks = 0;
    for (uint32 spellId : spellIds)
        if (Aura* aura = unit->GetAura(spellId))
            stacks = std::max<uint32>(stacks, aura->GetStackAmount());
    return stacks;
}

// Inferno is up on Geddon, or (CoA) its cast is in progress.
inline bool IsInfernoActive(Unit const* geddon)
{
    return HasAnyAura(geddon, INFERNO_AURAS) || geddon->FindCurrentSpellBySpellId(SPELL_INFERNO_COA);
}

// CoA: Geddon is casting the Living Bomb dummy at this unit; the debuff lands
// when the cast completes (CoaBossAI.cpp).
inline bool IsLivingBombCastAt(Unit const* geddon, Unit const* target)
{
    Spell* spell = geddon->FindCurrentSpellBySpellId(SPELL_LIVING_BOMB_COA_CAST);
    return spell && spell->m_targets.GetUnitTargetGUID() == target->GetGUID();
}

// Matches the base entry or one of its CoA difficulty copies. A spawn that
// carries the base entry keeps it on every difficulty; the copies only show
// up on a creature that was created by its copy entry. Defensive: the current
// CoA Molten Core spawns all use the base entry, so today this matches the
// same units as before (the server itself maps back with entry % 100000).
inline bool IsMcEntry(uint32 entry, uint32 baseEntry)
{
    return entry == baseEntry || entry == baseEntry + MC_ENTRY_OFFSET_HEROIC ||
           entry == baseEntry + MC_ENTRY_OFFSET_MYTHIC || entry == baseEntry + MC_ENTRY_OFFSET_ASCENDED;
}
}  // namespace MoltenCoreHelpers

#endif
