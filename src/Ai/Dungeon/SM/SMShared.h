/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_SMSHARED_H
#define PLAYERBOTS_SMSHARED_H

#include "Common.h"
#include "Unit.h"
#include <type_traits>

namespace SMShared
{

template <typename T, std::enable_if_t<std::is_enum_v<T>, int> = 0>
constexpr uint32 Id(T value)
{
    return static_cast<uint32>(value);
}

enum class SMSpells : uint32
{
    SPELL_DETONATION    = 9435,
    SPELL_ARCANE_BUBBLE = 9438,
};

inline constexpr uint32 SM_MAP_ID = 189;

// Detonation hits every enemy within 30 yards of Doan; stop a few yards past that edge.
inline constexpr float DETONATION_SAFE_DISTANCE = 36.0f;

// At 50% health Doan casts Arcane Bubble, then immediately starts a 5s Detonation cast.
inline bool IsDetonationImminent(Unit* doan)
{
    return doan->HasAura(Id(SMSpells::SPELL_ARCANE_BUBBLE)) ||
        doan->FindCurrentSpellBySpellId(Id(SMSpells::SPELL_DETONATION));
}

}

#endif
