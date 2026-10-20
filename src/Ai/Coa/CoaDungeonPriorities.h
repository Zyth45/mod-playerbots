/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

/*
 * Dungeon priorities (AiPlayerbot.CoaDungeonPriorities): what a group of bots should deal with
 * first in a dungeon fight, as players do.
 *
 * - Interrupts: a heal, crowd control or summon is kicked before an ordinary cast, and a short list
 *   of classic dungeon boss casts (the heals, crowd control and big group hits of
 *   bot-boss-tactics.md) before anything else.
 * - Adds: a totem, a ward or a creature that heals or shields its side is killed before the boss.
 *
 * The generic rules read the server's spell data, so they hold for every creature; the tables only
 * add what the data does not tell (a big hit looks like any other bolt). Spell ids of the tables
 * are checked against the spell store the first time they are read: an id the server lacks is
 * dropped and counted in the log.
 */

#ifndef PLAYERBOTS_COADUNGEONPRIORITIES_H
#define PLAYERBOTS_COADUNGEONPRIORITIES_H

#include "Define.h"

class Creature;
class SpellInfo;

namespace CoaDungeonPriorities
{
// 2 for a cast of the table, 1 for a heal, crowd control or summon, 0 for any other cast.
uint8 InterruptRank(SpellInfo const* info);

// Whether this enemy creature keeps its side alive: a totem, an add of the table (Healing Ward,
// Earthgrab Totem, Ward of Zum'rah) or a creature whose spells heal or shield its allies. Bosses
// never count: they are the target anyway.
bool HealsOrProtects(Creature* creature);

enum class Decision : uint8
{
    Kick,
    Purge,
    Add,
    Max
};

// True at most once a minute for each kind of decision, across all bots: one log line to measure
// how often a rule acts without flooding the log.
bool LogDue(Decision decision);
}

#endif
