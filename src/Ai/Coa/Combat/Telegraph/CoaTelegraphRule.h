/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COA_TELEGRAPH_RULE_H
#define PLAYERBOTS_COA_TELEGRAPH_RULE_H

#include <cmath>
#include <cstdint>

// Telegraphs (CoA Combat, Targorr): a ground marker that announces a hit. CoA's kits put a persistent area aura of type
// DUMMY on the landing spot of a leap or a slam a few seconds before it falls. Stock "avoid aoe" only flees periodic
// auras (AreaDebuffValue), so a dummy marker is ignored and a bot at full health stands in it until the one-shot lands
// (Stockade, Targorr the Dread in Mythic: every death a damage dealer at 100 % health inside the 4 yd circle).
//
// The table lists the marker spells, checked against the CoA client data (Spell.dbc: effect 27 PERSISTENT_AREA_AURA,
// aura 4 DUMMY, target 16 UNIT_DEST_AREA_ENEMY). The bot leaves the marker's radius plus a margin for as long as it
// stands, melee on the boss included. A new telegraph is one more row.
namespace CoaTelegraphRule
{
    struct Telegraph
    {
        std::uint32_t spellId;
        float margin;  // yards beyond the marker's own radius
    };

    inline constexpr float DEFAULT_MARGIN = 1.5f;

    inline constexpr Telegraph TELEGRAPHS[] = {
        {2102735, DEFAULT_MARGIN},  // Berserker Leap Dest Area (ASC D0), 4 yd, 3.5 s: Targorr the Dread, Stockade
        {2100633, DEFAULT_MARGIN},  // Berserker Leap Dest Area (ASC D0), same marker on another kit
    };

    inline Telegraph const* Find(std::uint32_t spellId)
    {
        for (Telegraph const& t : TELEGRAPHS)
            if (t.spellId == spellId)
                return &t;
        return nullptr;
    }

    // How far from the marker's centre the bot must be to be safe.
    inline float SafeDistance(Telegraph const& t, float markerRadius) { return markerRadius + t.margin; }

    // Whether a bot `distance` yards from the centre of a marker of `markerRadius` yards must move.
    inline bool MustLeave(Telegraph const& t, float markerRadius, float distance)
    {
        return markerRadius > 0.0f && distance < SafeDistance(t, markerRadius);
    }

    // Where the bot steps to: straight out from the marker's centre, through its own position, `distance` yards from
    // the centre (bench a12: the stock flee sends a melee bot left or right of its target, around the circle rather
    // than out of it). A bot on the centre itself leaves along `fallbackAngle`; `turn` rotates the way out (radians)
    // when a wall blocks the straight one.
    inline void ExitPoint(float centreX, float centreY, float botX, float botY, float fallbackAngle, float turn,
                          float distance, float& x, float& y)
    {
        float const dx = botX - centreX;
        float const dy = botY - centreY;
        float angle = (dx * dx + dy * dy) > 0.01f ? std::atan2(dy, dx) : fallbackAngle;
        angle += turn;
        x = centreX + std::cos(angle) * distance;
        y = centreY + std::sin(angle) * distance;
    }
}

#endif
