/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef COA_STUCK_COMBAT_H
#define COA_STUCK_COMBAT_H

#include "Action.h"
#include "CoaStuckCombatRule.h"
#include "Trigger.h"

#include <cstdint>
#include <functional>

class Player;

// Stuck combat breaker (CoA Combat, guard B3, from Dungeon Clear's phantom-combat hatch). A grouped bot in a dungeon
// or raid, flagged in combat with nothing hitting it, no victim, and no unit holding it that is both legitimate (alive,
// not evading, within 100 yd, reachable, not a trigger, allowed by its script to attack) and prosecuting the fight (a
// seen holder within 22 yd, or one closing on it), has its combat and threat cleared after the timeout (15 s), like a
// GM ".combatstop". Never during a raid encounter (the boss stand-down); a combat with no reference at all (forced by a
// script) is never touched.
//
// It acts for the members of a Dungeon Clear run, as before, unless the run's hook says otherwise; outside a run when
// AiPlayerbot.CoaCombat.StuckCombat.Enable is 1 (0 by default).
inline constexpr char const* CoaStuckCombatGuard = "stuck combat breaker";

namespace CoaStuckCombat
{
    // Whether a Dungeon Clear run lets the breaker clear this member's combat right now. Dungeon Clear: a live,
    // unpaused run led by a bot tank (DcPlayerLedDecision::RungsFor(...).breakStuckCombat), and not Karazhan's chess
    // game (DcKarazhan::ChessIsOn). Set once, at load, before the maps update. Unset, a run always lets it.
    using RunAllows = std::function<bool(Player* member)>;
    void SetRunAllows(RunAllows hook);

    // Whether the breaker acts for this bot at all (scope, run, settings), before any combat read.
    bool Acts(Player* bot);
}

// "coa combat break stuck combat": true once the phantom combat has held for the timeout. Checked once a second.
class CoaBreakStuckCombatTrigger : public Trigger
{
public:
    CoaBreakStuckCombatTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa combat break stuck combat", 1000) {}

    bool IsActive() override;

private:
    void ResetClocks();

    // Per bot (the context makes one trigger per bot, shared by both engines).
    std::uint32_t stuckSinceMs = 0;
    CoaStuckCombatRule::ClosingWatch holderWatch;
};

// "coa combat break stuck combat": ends the bot's combat and takes it off every threat list.
class CoaBreakStuckCombatAction : public Action
{
public:
    CoaBreakStuckCombatAction(PlayerbotAI* botAI) : Action(botAI, "coa combat break stuck combat") {}

    bool Execute(Event event) override;
};

#endif
