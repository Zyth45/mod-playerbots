/*
 * The spec a CoA bot actually plays, and what follows from it.
 *
 * CoaSpecStrategies.h is pure data and knows nothing about a Player. This
 * header adds the one lookup everything else needs: from a character to its
 * row in that table.
 *
 * A bot that has no specialization yet falls back to the default of its class,
 * the first row. That keeps a fresh bot playable: it gets a position, a
 * rotation and a role before anyone has chosen anything for it.
 */

#ifndef PLAYERBOTS_COASPECLOOKUP_H
#define PLAYERBOTS_COASPECLOOKUP_H

#include "AscensionSpecialization.h"
#include "CoaSpecStrategies.h"
#include "ItemTemplate.h"
#include "Player.h"

// True for the 21 Conquest of Azeroth classes.
inline bool IsCoaClass(Player const* player)
{
    if (!player)
        return false;
    uint8 const classId = player->getClass();
    return classId >= 12 && classId <= 32;
}

// The row for this character, or nullptr if it is not a CoA class.
inline CoaSpecStrategy const* GetCoaSpecStrategyFor(Player const* player)
{
    if (!IsCoaClass(player))
        return nullptr;

    uint8 const classId = player->getClass();
    if (uint32 const specId = GetAscensionActiveSpecialization(player))
        if (CoaSpecStrategy const* known = GetCoaSpecStrategy(classId, uint16(specId)))
            return known;

    // No spec set, or one we do not know: the class default.
    return GetCoaDefaultSpecStrategy(classId);
}

inline bool IsCoaRole(Player const* player, CoaSpecRole role)
{
    CoaSpecStrategy const* spec = GetCoaSpecStrategyFor(player);
    return spec && spec->role == role;
}

// A spec whose rotation fires a real ranged weapon, so it must not be handed a wand or a relic: the
// ranged slot holds the bow or the idol, never both. Starcaller Moon Priest heals through Huntress
// Shot: the shot lays the Scattered Stars that Lunar Lance consumes, and in Aspect of the Goddess every
// star consumed heals the allies around (half of a real player's healing, 25/09). Starcaller Sentinel
// deals half its damage with Starfire Shot, Huntress Shot and Trueshot; its intellect weights rank an
// epic idol above any bow, and with the idol none of the three can be cast (boss bench of 02/10: 65
// Sentinels of 73 held one). Ranger, Witch Hunter, Tinker and Shadowhunting are never offered a relic
// there, and Moon Guard and Warden only keep the shots as fillers. The spec must be the one the
// character has chosen: a Starcaller without one is not taken for the default Sentinel here.
inline bool CoaSpecNeedsShootingWeapon(Player const* player)
{
    if (!player || player->getClass() != 26)
        return false;
    uint32 const specId = GetAscensionActiveSpecialization(player);
    return specId == 43 || specId == 44;
}

// What the ranged slot holds cannot fire the shots this spec needs: nothing, a wand or a relic.
inline bool CoaRangedSlotCannotShoot(Player const* player, ItemTemplate const* proto)
{
    if (!CoaSpecNeedsShootingWeapon(player))
        return false;
    return !proto || proto->Class != ITEM_CLASS_WEAPON || proto->SubClass == ITEM_SUBCLASS_WEAPON_WAND;
}

#endif
