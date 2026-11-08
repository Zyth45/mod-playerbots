/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef COA_ROOM_BOSS_CHASE_H
#define COA_ROOM_BOSS_CHASE_H

#include <cmath>
#include <cstdint>
#include <vector>

// The sleeping room boss chase guard, from Dungeon Clear: no stock reach / chase (nor a gap-closer: Charge, Intercept,
// Feral Charge) whose END lies inside the wake sphere of a room-aggro boss that still sleeps. A fight with room trash
// runs on the stock combat movers, which follow a mob wherever it stands; carried onto the boss's steps it wakes him,
// and his CallForHelp wakes the room (BRD, Emperor Dagran Thaurissan: four of five dead). Pure and unchanged:
// DungeonClearMath::ChaseEndToBoss / ChaseWakesSleepingBoss, DcEngageGeometry::AggroReachYards and the skirt override
// of RoomAggroSphereRadius; the rows are a read-only copy of Dungeon Clear's RoomAggroRegistry (CoA content only).
namespace CoaRoomBossChase
{
    // How far from the boss a chase toward a target ENDS (2D): a mover with reach `range` stops that far short of the
    // target on its own side, one already inside it does not move. range 0: it ends on the target (melee, a leap).
    inline float ChaseEndToBoss(float botX, float botY, float targetX, float targetY, float bossX, float bossY,
                                float range)
    {
        float const dx = botX - targetX;
        float const dy = botY - targetY;
        float const toBot = std::sqrt(dx * dx + dy * dy);
        float endX = targetX;
        float endY = targetY;
        if (toBot <= range)
        {
            endX = botX;
            endY = botY;
        }
        else if (range > 0.0f)
        {
            endX = targetX + dx / toBot * range;
            endY = targetY + dy / toBot * range;
        }
        return std::sqrt((endX - bossX) * (endX - bossX) + (endY - bossY) * (endY - bossY));
    }

    inline bool ChaseWakesSleepingBoss(float chaseEndToBoss, float wakeSphere, bool bossInCombat)
    {
        return !bossInCombat && chaseEndToBoss < wakeSphere;
    }

    // The aggro reach formula: the boss's aggro range on the bot, both combat reaches, the margin, the party buffer.
    inline float AggroReachYards(float aggroRange, float mobReach, float botReach, float aggroMargin, float partyBuffer)
    {
        return aggroRange + mobReach + botReach + aggroMargin + partyBuffer;
    }

    // RoomAggroSphereRadius: the computed sphere, widened (never shrunk) by the boss's skirt override.
    inline float WakeSphere(float computed, float skirtOverride)
    {
        return skirtOverride > computed ? skirtOverride : computed;
    }

    struct RoomBoss
    {
        std::uint32_t mapId;
        std::uint32_t bossEntry;
        float skirtRadius; // 0: the computed sphere alone
    };

    // Dungeon Clear's RoomAggroRegistry rows for CoA's classic dungeons (radius / whitelist / camp data are the room
    // clear's, which stays in Dungeon Clear: the guard only needs the boss and its skirt). Left out: the BC and WotLK
    // rows (Shadow Labyrinth, Mana-Tombs, Azjol-Nerub, Trial of the Champion, Gundrak, the Mechanar, Karazhan).
    inline constexpr RoomBoss ROOM_BOSSES[] = {
        {189, 3976, 0.0f},  // Scarlet Monastery Cathedral: Highlord Mograine
        {189, 3977, 0.0f},  // Scarlet Monastery Cathedral: Inquisitor Whitemane
        {289, 10432, 0.0f}, // Scholomance: Vectus
        {289, 10433, 0.0f}, // Scholomance: Marduk Blackpool
        {109, 5710, 0.0f},  // Sunken Temple: Jammal'an the Prophet
        {230, 9019, 0.0f},  // Blackrock Depths: Emperor Dagran Thaurissan
        {429, 14354, 0.0f}, // Dire Maul East: Pusillin
        {229, 10363, 0.0f}, // Upper Blackrock Spire: General Drakkisath
        {36, 1763, 0.0f},   // Deadmines: Gilnid
    };

    inline RoomBoss const* Find(std::uint32_t mapId, std::uint32_t bossEntry)
    {
        for (RoomBoss const& b : ROOM_BOSSES)
            if (b.mapId == mapId && b.bossEntry == bossEntry)
                return &b;
        return nullptr;
    }

    inline std::vector<RoomBoss const*> ForMap(std::uint32_t mapId)
    {
        std::vector<RoomBoss const*> out;
        for (RoomBoss const& b : ROOM_BOSSES)
            if (b.mapId == mapId)
                out.push_back(&b);
        return out;
    }

    inline float SkirtOverride(std::uint32_t mapId, std::uint32_t bossEntry)
    {
        RoomBoss const* b = Find(mapId, bossEntry);
        return b ? b->skirtRadius : 0.0f;
    }

    // Which branch rules for this bot (before the geometry):
    //   RunBoss: the reference tank drives a Dungeon Clear run whose room clear names the next boss; every member,
    //     tank included, is kept out of THAT boss's sphere (sized on the run tank), as in Dungeon Clear;
    //   MapBosses: outside a Dungeon Clear run (a human Main Tank, a bot main tank in normal play): every flagged boss
    //     of the map within 150yd that sleeps, sphere sized on the bot; a bot tank under a HUMAN reference tank is
    //     exempt;
    //   None: the guard is off.
    enum class Branch
    {
        None,
        RunBoss,
        MapBosses
    };

    struct BranchInputs
    {
        bool chaseAction = false; // a ReachTargetAction or a CastReachTargetSpellAction
        bool hasTank = false;     // reference tank, alive, in the bot's instance
        bool tankIsHuman = false;
        bool inDcRun = false;     // the bot is in an active Dungeon Clear run
        bool botIsTank = false;
    };

    // In a Dungeon Clear run the guard rules only on the run's room boss (Dungeon Clear 2.9.4: no room clear active,
    // no guard); outside a run it guards every sleeping flagged boss of the map.
    constexpr Branch ChooseBranch(BranchInputs const& in)
    {
        if (!in.chaseAction || !in.hasTank)
            return Branch::None;
        if (in.inDcRun && !in.tankIsHuman)
            return Branch::RunBoss;
        if (in.tankIsHuman && in.botIsTank)
            return Branch::None;
        return Branch::MapBosses;
    }
}

#endif
