/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_DMTRIGGERS_H
#define PLAYERBOTS_DMTRIGGERS_H

#include <vector>

#include "DungeonBossHelpers.h"
#include "GenericTriggers.h"
#include "Trigger.h"

// Dire Maul (map 429).
namespace DireMaul
{
    // Hydrospawn's Massive Geyser (22421): a 1.5 s cast on a player, then a geyser (14122) at that player's spot
    // that casts Water (22422) every second for 2 s: 1633-1882 Frost and a strong knockback within 10 yd. His old
    // script and his CoA kit both cast it, every 20-35 s.
    constexpr uint32 NPC_MASSIVE_GEYSER = 14122;
    constexpr float GEYSER_SAFE = 13.0f;
    constexpr float GEYSER_SEARCH = 30.0f;

    // Geyser (2100220) hits the player farthest from him and everyone within 10 yd of that player: the ranged and
    // healers keep that far from each other, within reach of him.
    constexpr float SPREAD_TOO_CLOSE = 8.0f;
    constexpr float SPREAD_WANTED = 10.5f;
    constexpr float SPREAD_MIN_FROM_BOSS = 8.0f;
    constexpr float SPREAD_MAX_FROM_BOSS = 30.0f;

    // A Hydroling (14350) every 6 s from his kit, one a minute from his old script, each for 60 s: left alone they
    // pile up to about ten. The damage dealers clear them once this many are up.
    constexpr uint32 NPC_HYDROLING = 14350;
    constexpr uint32 HYDROLING_SWITCH = 3;
    constexpr float HYDROLING_SEARCH = 30.0f;

    Unit* Hydrospawn(PlayerbotAI* botAI, AiObjectContext* context);
    // Where a geyser stands or is about to rise. The player a Massive Geyser is being cast on is left out for himself:
    // he moves away from the group instead, so that it rises away from everyone.
    std::vector<Position> GeyserCenters(Player* bot, Unit* hydrospawn);
    bool IsGeyserTarget(Player* bot, Unit* hydrospawn);
    bool SafeFromGeysers(std::vector<Position> const& centers, float x, float y);
    bool KeepsSpread(Player* bot);
    std::vector<Unit*> Hydrolings(PlayerbotAI* botAI, Player* bot, AiObjectContext* context);
}

class DmMassiveGeyserTrigger : public Trigger
{
public:
    DmMassiveGeyserTrigger(PlayerbotAI* ai) : Trigger(ai, "dm massive geyser") {}
    bool IsActive() override;
};

class DmGeyserSpreadTrigger : public Trigger
{
public:
    DmGeyserSpreadTrigger(PlayerbotAI* ai) : Trigger(ai, "dm geyser spread", 1000) {}
    bool IsActive() override;
};

class DmHydrolingsTrigger : public Trigger
{
public:
    DmHydrolingsTrigger(PlayerbotAI* ai) : Trigger(ai, "dm hydrolings", 500) {}
    bool IsActive() override;
};

#endif
