/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COA_MOUNT_RULE_H
#define PLAYERBOTS_COA_MOUNT_RULE_H

#include <cstdint>

// When the mount check (CheckMountStateAction) may run at all, with no game types for the unit test.
//
// CoA lets a player ride before AiPlayerbot.UseGroundMountAtMinLevel, and an alt below that level rides along while
// its master rides. The check used to be allowed below that level only while the master was mounted: once the master
// got off to fight, a mounted alt lost the very check that dismounts it, and stayed in the saddle on top of the enemy
// without attacking until it looted (Discord, RandomGnome). A mounted bot may always run the check, to get off.
namespace CoaMountRule
{
    inline bool CheckAllowed(unsigned botLevel, unsigned minMountLevel, bool botMounted, bool followsMountedMaster)
    {
        return botLevel >= minMountLevel || botMounted || followsMountedMaster;
    }

    // An alt with a master gets off when the master is on foot (dismounted, or fighting on foot).
    inline bool DismountForMaster(bool masterMounted, bool botMounted)
    {
        return botMounted && !masterMounted;
    }
// CoA 3.2.2: the riding a random bot learns at its level: Novice (33379) under APPRENTICE_RIDING_LEVEL, Apprentice
// (33388) from it (CoA's trainer teaches 33388 at level 20; Novice Riding is CoA's own step at 50 skill, the quest at 5).
// 33379 is the riding itself (SPELL_EFFECT_SKILL 762); 33378 is the trainer's spell that teaches it (LEARN_SPELL 33379,
// SKILL_STEP): learned as a spell, it gave no riding skill (sonde of 5f662017: the bots' riding fell at each save).
constexpr uint32_t NOVICE_RIDING_SPELL = 33379;
constexpr uint32_t NOVICE_RIDING_TEACHER = 33378;
constexpr uint32_t APPRENTICE_RIDING_SPELL = 33388;
constexpr uint32_t APPRENTICE_RIDING_LEVEL = 20;

inline uint32_t NoviceOrApprentice(uint32_t level)
{
    return level >= APPRENTICE_RIDING_LEVEL ? APPRENTICE_RIDING_SPELL : NOVICE_RIDING_SPELL;
}

// CoA 3.2.2 (the user: most bots ride the same elekk): CoA's ground mounts all run at the rider's riding speed, not the
// spell's, so a random bot keeps one mount of its own, the same each time: the start of a draw from its guid among the
// ground mounts it knows, sorted. 0: none.
inline uint32_t StableMountIndex(uint64_t guid, uint32_t count)
{
    if (!count)
        return 0;
    uint64_t z = guid + 0x9E3779B97F4A7C15ull;  // splitmix64
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return uint32_t(z % count);
}
}

#endif
