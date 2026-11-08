/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COA_TANK_FORM_H
#define PLAYERBOTS_COA_TANK_FORM_H

class PlayerbotAI;

// Bear form before the pull (CoA Combat, guard B1).
//
// Stock shifts a feral tank only on the combat engine (the "bear form" trigger belongs to the bear strategy, a combat
// strategy). A pull is opened out of combat, so the druid tagged the pack in caster form and took its opener in cloth
// before shifting. An autopilot that drives its own pull (Dungeon Clear) calls EnsureBearForm on the way in.
namespace CoaTankForm
{
    // A druid that fights as a bear: it runs the "bear" combat strategy and knows a bear form. Keyed on the strategy,
    // not on the spec test, which for a druid asks whether it is already in bear form.
    bool IsBearTank(PlayerbotAI* botAI);

    // When a bear tank is not in Bear or Dire Bear form, shift it now (Dire Bear when trained). Fire and forget: true
    // when the shift was cast this tick; a shift that cannot go out (cooldown, mana, silence) is simply tried again on
    // a later tick, and nothing may wait on it.
    bool EnsureBearForm(PlayerbotAI* botAI);
}

#endif
