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

#endif
