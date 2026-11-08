/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef COA_TANK_FIGHT_ASSIST_H
#define COA_TANK_FIGHT_ASSIST_H

#include "MovementActions.h"
#include "ObjectGuid.h"
#include "Trigger.h"
#include "Value.h"

#include <unordered_map>

// The assist on the reference tank's fight (combat guards that came from Dungeon Clear, 07/11: the "tank's fight" part
// of DungeonClearAssistCamp{,Combat}Trigger and DungeonClearAssistCampActionBase; the camp and pull branches stay in
// Dungeon Clear). A damage dealer that the tank's fight never reached (around a corner, past its engage range) or that
// is flagged in combat with nothing in sight is put on a mob of that fight: the non-combat side seeds the target and
// switches to the combat engine, the combat side closes on the mob until it is in sight, then leaves the fight to the
// stock rotation and reach actions.

// Per bot: the mob the close-in was refused on, and the mobs given up (no route) for a while.
struct CoaTankFightAssistState
{
    ObjectGuid refusedTarget;
    uint32 refusedSinceMs = 0;
    std::unordered_map<uint64, uint32> givenUpUntilMs;

    bool GivenUp(ObjectGuid guid, uint32 now) const;
    void GiveUp(ObjectGuid guid, uint32 now, uint32 forMs);
};

// "coa tank fight assist state"
class CoaTankFightAssistStateValue : public ManualSetValue<CoaTankFightAssistState&>
{
public:
    CoaTankFightAssistStateValue(PlayerbotAI* botAI)
        : ManualSetValue<CoaTankFightAssistState&>(botAI, data, "coa tank fight assist state")
    {
    }

    void Reset() override { data = CoaTankFightAssistState(); }

private:
    CoaTankFightAssistState data;
};

// "coa tank fight assist", non-combat engine: a damage dealer while the tank fights and the lead has run, whether or
// not it is flagged in combat (a member flagged by the group's fight with nothing of its own stays in the non-combat
// engine, and nothing else would move it).
class CoaTankFightAssistTrigger : public Trigger
{
public:
    CoaTankFightAssistTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa tank fight assist") {}
    bool IsActive() override;
};

// "coa tank fight assist combat", combat engine: the same, while the bot has no mob it can hit from where it stands
// (in reach and in sight). Goes quiet as soon as one is: the rotation owns the fight.
class CoaTankFightAssistCombatTrigger : public Trigger
{
public:
    CoaTankFightAssistCombatTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa tank fight assist combat") {}
    bool IsActive() override;
};

class CoaTankFightAssistAction : public MovementAction
{
public:
    // "coa tank fight assist" (non-combat engine) and "coa tank fight assist combat" (combat engine).
    CoaTankFightAssistAction(PlayerbotAI* botAI, std::string const name, bool combatEngine)
        : MovementAction(botAI, name), combatEngine(combatEngine)
    {
    }

    bool Execute(Event event) override;

private:
    Unit* PickFightTarget(Player* tank, CoaTankFightAssistState const& state, uint32 now);

    bool const combatEngine;
};

#endif
