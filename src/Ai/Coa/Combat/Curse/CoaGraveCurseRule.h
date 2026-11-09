/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COA_GRAVE_CURSE_RULE_H
#define PLAYERBOTS_COA_GRAVE_CURSE_RULE_H

#include <initializer_list>

// Call of the Grave, Dire Maul West (Eldreth Spectre 11473, Eldreth Phantasm 11475): an instant curse on every enemy
// within 5 yd that deals all its damage at once after 60 s: 17831, swapped by SpellDifficulty for 159900 in heroic and
// 387251 in mythic (10 671 damage there, more than a level 60 damage dealer's health). No game types, for the test.
namespace CoaGraveCurseRule
{
    inline constexpr unsigned CURSES[] = {17831u, 159900u, 387251u};
    inline constexpr unsigned CASTERS[] = {11473u, 11475u};
    // The curse reaches 5 yd; the melee damage dealers keep 6.5 yd.
    inline constexpr float CURSE_YARDS = 5.0f;
    inline constexpr float KEEP_YARDS = 6.5f;

    inline bool IsGraveCurse(unsigned spellId)
    {
        for (unsigned id : CURSES)
            if (id == spellId)
                return true;
        return false;
    }

    // The caster entries, with the four difficulty copies some CoA dungeons use (+100000 per difficulty).
    inline bool IsGraveCaster(unsigned entry)
    {
        for (unsigned id : CASTERS)
            for (unsigned offset : {0u, 100000u, 200000u, 300000u})
                if (entry == id + offset)
                    return true;
        return false;
    }

    // A melee damage dealer within reach of a caster's curse steps out; tanks hold them, ranged are out of reach.
    inline bool MustLeave(bool isTank, bool isMelee, float distanceToCaster)
    {
        return !isTank && isMelee && distanceToCaster < KEEP_YARDS;
    }
}

#endif
