/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef COA_GARDES_GATE_H
#define COA_GARDES_GATE_H

// Pure rules shared by the party combat guards that came from Dungeon Clear (07/11): combat regroup, the escape leap
// ban, the sleeping room boss chase guard, the Lyceum close-up and the Iron Hall safe spot. No engine header, so the
// assert-main tests in tests/ read it as is.
namespace CoaGardesGate
{
    // The names the guards are known by in CoaGroupTank::ModuleGuardActs / SetHandledByDungeonClear.
    inline constexpr char const* COMBAT_REGROUP = "combat regroup";
    inline constexpr char const* ESCAPE_LEAP_BAN = "escape leap ban";
    inline constexpr char const* ROOM_BOSS_CHASE = "room boss chase";
    inline constexpr char const* LYCEUM_CLOSE_UP = "lyceum close up";
    inline constexpr char const* IRON_HALL = "iron hall";

    // Dungeon Clear's DcPlayerLedDecision::MemberRungApplies, unchanged. The guards that MOVE a bot toward the tank or
    // onto his fight leave a bot tank alone while a HUMAN is the reference tank: it is his off-tank and goes where his
    // orders send it. With a bot tank as the reference (a Dungeon Clear run, a bot main tank), every member keeps the
    // guard.
    constexpr bool MemberRungApplies(bool tankIsHuman, bool botIsTank)
    {
        return !(tankIsHuman && botIsTank);
    }
}

#endif
