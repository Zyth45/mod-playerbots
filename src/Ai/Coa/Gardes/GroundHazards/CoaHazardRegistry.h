/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef COA_HAZARD_REGISTRY_H
#define COA_HAZARD_REGISTRY_H

#include <cmath>
#include <cstdint>
#include <vector>

// Static registry of persistent ground damage the combat AI cannot reason about on its own: the ground hazards
// guard, moved from Dungeon Clear (DcHazardRegistry) into the module so it acts in every group, not only in a DC run.
// Header-only and free of game state, so the standalone tests can compile it alone.
//
// THREE KINDS of emitter, because the game represents ground damage three different ways:
//
//   * CoaHazardEmitter — a CREATURE carrying a permanent pulsing aura. Keyed on (map, creature entry); resolved live
//     off the creature's position (the Destroyed Sentinel, Maraudon's Creeping Sludge, Gehennas' flame orb).
//   * CoaGroundHazard — a PERSISTENT AREA AURA: a DynamicObject at the cast point, ticking until its duration expires.
//     Keyed on (map, spell id) (Scholomance's Cloud of Disease, Maraudon's Noxious Cloud, Molten Core's Rain of Fire).
//   * CoaTrapHazard — a GAMEOBJECT_TYPE_TRAP dropped by a script. Keyed on (map, gameobject entry).
//
// Two threats per row: `radius` is a placement keep-out (do not camp or route through it), and `vacateRadius` > 0
// marks a pulse the party must actively LEAVE (CoaHazardVacateTrigger / CoaHazardVacateAction). `holdBand` says how
// far past the pulse a bot still reads in danger, `retreatSlack` how far past it the retreat aims; retreatSlack must
// exceed holdBand or the bot arrives still in danger and flees again forever. The full rationale of each row lives
// with the data in CoaHazardRegistryData.h.
struct CoaHazardEmitter
{
    std::uint32_t mapId{0};
    std::uint32_t creatureEntry{0};
    float radius{0.0f};
    float zBand{12.0f};
    float vacateRadius{0.0f};
    float holdBand{2.0f};
    float retreatSlack{6.0f};
};

struct CoaGroundHazard
{
    std::uint32_t mapId{0};
    std::uint32_t spellId{0};  // DynamicObject::GetSpellId(): the CAST spell, not the per-tick one
    float radius{0.0f};
    float zBand{6.0f};
    float vacateRadius{0.0f};
    float holdBand{2.0f};
    float retreatSlack{6.0f};
};

struct CoaTrapHazard
{
    std::uint32_t mapId{0};
    std::uint32_t goEntry{0};  // GameObject::GetEntry() of the trap
    float radius{0.0f};
    float zBand{6.0f};
    float vacateRadius{0.0f};  // the trap's CAST-spell radius, not its trigger diameter
    float holdBand{2.0f};
    float retreatSlack{6.0f};
};

#include "CoaHazardRegistryData.h"

namespace CoaHazardRegistry
{
    inline bool HasEmitters(std::uint32_t mapId)
    {
        for (auto const& e : CoaHazardData::kEmitters)
            if (e.mapId == mapId)
                return true;
        return false;
    }

    inline bool HasGroundHazards(std::uint32_t mapId)
    {
        for (auto const& g : CoaHazardData::kGroundHazards)
            if (g.mapId == mapId)
                return true;
        return false;
    }

    inline bool HasTrapHazards(std::uint32_t mapId)
    {
        for (auto const& t : CoaHazardData::kTrapHazards)
            if (t.mapId == mapId)
                return true;
        return false;
    }

    // A row of ANY kind: the cheap early-out of the live predicates and the vacate trigger, so a map that registers
    // only a pool (Scholomance) or only a trap is not skipped by a creature-only check.
    inline bool HasAnyHazard(std::uint32_t mapId)
    {
        return HasEmitters(mapId) || HasGroundHazards(mapId) || HasTrapHazards(mapId);
    }

    inline CoaHazardEmitter const* Find(std::uint32_t mapId, std::uint32_t creatureEntry)
    {
        for (auto const& e : CoaHazardData::kEmitters)
            if (e.mapId == mapId && e.creatureEntry == creatureEntry)
                return &e;
        return nullptr;
    }

    inline CoaGroundHazard const* FindGround(std::uint32_t mapId, std::uint32_t spellId)
    {
        for (auto const& g : CoaHazardData::kGroundHazards)
            if (g.mapId == mapId && g.spellId == spellId)
                return &g;
        return nullptr;
    }

    inline CoaTrapHazard const* FindTrap(std::uint32_t mapId, std::uint32_t goEntry)
    {
        for (auto const& t : CoaHazardData::kTrapHazards)
            if (t.mapId == mapId && t.goEntry == goEntry)
                return &t;
        return nullptr;
    }

    // Every registered trap entry on `mapId`, in table order: the live value sweeps by entry rather than every
    // gameobject of the floor.
    inline std::vector<std::uint32_t> TrapEntries(std::uint32_t mapId)
    {
        std::vector<std::uint32_t> entries;
        for (auto const& t : CoaHazardData::kTrapHazards)
            if (t.mapId == mapId)
                entries.push_back(t.goEntry);
        return entries;
    }

    // Squared 2D distance from P to the segment (A,B) (DungeonClearMath::DistSqToSegment2D, unchanged).
    inline float DistSqToSegment2D(float px, float py, float ax, float ay, float bx, float by)
    {
        float const ex = bx - ax;
        float const ey = by - ay;
        float const len2 = ex * ex + ey * ey;
        if (len2 <= 1e-6f)
        {
            float const dx = px - ax;
            float const dy = py - ay;
            return dx * dx + dy * dy;
        }
        float t = ((px - ax) * ex + (py - ay) * ey) / len2;
        if (t < 0.0f)
            t = 0.0f;
        else if (t > 1.0f)
            t = 1.0f;
        float const cx = ax + t * ex;
        float const cy = ay + t * ey;
        float const dx = px - cx;
        float const dy = py - cy;
        return dx * dx + dy * dy;
    }

    // True when (px,py,pz) lies inside a keep-out cylinder of `radius`/`zBand` centred on (ex,ey,ez). A radius of 0
    // makes the cylinder inert.
    inline bool PointInCylinder(float radius, float zBand, float ex, float ey, float ez, float px, float py, float pz)
    {
        if (radius <= 0.0f)
            return false;
        if (std::fabs(pz - ez) > zBand)
            return false;

        float const dx = px - ex;
        float const dy = py - ey;
        return dx * dx + dy * dy < radius * radius;
    }

    // True when the 2D segment (a)->(b) clips the keep-out circle, with the leg touching the zBand around ez (a leg
    // whose two ends straddle the band on opposite sides still crosses it).
    inline bool SegmentClipsCylinder(float radius, float zBand, float ex, float ey, float ez, float ax, float ay,
                                     float az, float bx, float by, float bz)
    {
        if (radius <= 0.0f)
            return false;

        float const da = az - ez;
        float const db = bz - ez;
        if (std::fabs(da) > zBand && std::fabs(db) > zBand && (da > 0.0f) == (db > 0.0f))
            return false;

        float const clipSq = DistSqToSegment2D(ex, ey, ax, ay, bx, by);
        return clipSq < radius * radius;
    }

    // Row-typed wrappers over the two primitives, one pair per kind.
    template <class Row>
    inline bool PointInside(Row const& r, float ex, float ey, float ez, float px, float py, float pz)
    {
        return PointInCylinder(r.radius, r.zBand, ex, ey, ez, px, py, pz);
    }

    template <class Row>
    inline bool SegmentClips(Row const& r, float ex, float ey, float ez, float ax, float ay, float az, float bx,
                             float by, float bz)
    {
        return SegmentClipsCylinder(r.radius, r.zBand, ex, ey, ez, ax, ay, az, bx, by, bz);
    }
}

#endif
