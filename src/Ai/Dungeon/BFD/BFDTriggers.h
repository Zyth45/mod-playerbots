/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_BFDTRIGGERS_H
#define PLAYERBOTS_BFDTRIGGERS_H

#include "DungeonBossHelpers.h"
#include "GenericTriggers.h"
#include "Trigger.h"

// Blackfathom Deeps (map 48).
namespace BlackfathomDeeps
{
    using DungeonBoss::DegreesToRadians;

    // Ghamoo-ra's Torrent (2102687): a 3 s cast, then 20.5 s in which the boss turns clockwise once around itself
    // (spell_coa_torrent_spin) and every 0.25 s fires a 24 degree cone of 50 yd in front of him that hits and knocks
    // back (2102688). His back is the one place the beam never reaches while he turns, so every bot keeps to it.
    constexpr float TORRENT_REACH = 50.0f;
    constexpr float TORRENT_HALF_ARC = DegreesToRadians(12.0f);
    // How far from straight behind him a bot may drift, as he turns, before it moves again.
    constexpr float TORRENT_BACK_TOLERANCE = DegreesToRadians(35.0f);
    // At most a quarter turn per move, so that the way round never cuts through his body or his front.
    constexpr float TORRENT_STEP = DegreesToRadians(90.0f);
    constexpr float TORRENT_RANGED_DISTANCE = 20.0f;

    // Cast at the tank. Forked Lightning (8435, Lady Sarevess, a 2 s cast every 2.5-3 s) reaches 30 yd, Gelihast's
    // Cone of Cold (2102699, instant every 8 s) 15 yd; both are 24 degrees wide.
    constexpr DungeonBoss::FrontalCone FRONTAL_CONES[] = {
        { "lady sarevess", 30.0f, DegreesToRadians(12.0f), nullptr },
        { "gelihast", 15.0f, DegreesToRadians(12.0f), nullptr },
    };

    // Ghamoo-ra in this fight. While he turns he is passive, so he is also looked for among the enemies around the
    // bot, not only on its threat list.
    Unit* FindGhamoora(PlayerbotAI* botAI, AiObjectContext* context);

    bool IsTorrenting(Unit const* ghamoora);
    // Within reach of the beam and away from his back.
    bool OffHisBack(Unit const* ghamoora, Player* bot);
}

class BfdGhamooraTorrentTrigger : public Trigger
{
public:
    BfdGhamooraTorrentTrigger(PlayerbotAI* ai) : Trigger(ai, "bfd ghamoo-ra torrent") {}
    bool IsActive() override;
};

class BfdFrontalConeTrigger : public Trigger
{
public:
    BfdFrontalConeTrigger(PlayerbotAI* ai) : Trigger(ai, "bfd frontal cone") {}
    bool IsActive() override;
};

#endif
