/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef COA_GARDES_SOINS_H
#define COA_GARDES_SOINS_H

#include "MovementActions.h"
#include "ObjectGuid.h"
#include "PartyMemberToHeal.h"
#include "Strategy.h"
#include "Trigger.h"
#include "Value.h"

class Map;
class PlayerbotAI;
class Unit;

// The healer guards that came from Dungeon Clear (07/11, "DC is the autorun in the dungeon, the strategies are in
// playerbots"): they act for every healer bot, in a Dungeon Clear run, behind a player who tanks ("tank main on") and
// in plain play. Each stands down for the members of a Dungeon Clear run while Dungeon Clear still runs its own copy
// (CoaGroupTank::ModuleGuardActs, guards "heal reposition" and "top off party").

// The settings, read once at first use: AiPlayerbot.CoaXxx, with the player's old DungeonClear.Xxx read first.
struct CoaHealSettings
{
    bool healReposition;     // AiPlayerbot.CoaHealReposition / DungeonClear.HealReposition
    float hpFloor;           // AiPlayerbot.CoaHealRepositionHpFloor / DungeonClear.HealRepositionHpFloor, 1-100
    float tankBias;          // AiPlayerbot.CoaHealRepositionTankBias / DungeonClear.HealRepositionTankBias, 0-50
    float maxRange;          // AiPlayerbot.CoaHealRepositionMaxRange / DungeonClear.HealRepositionMaxRange, 20-120
    bool topOffParty;        // AiPlayerbot.CoaTopOffParty
    uint32 topOffHealthPct;  // AiPlayerbot.CoaTopOffHealthPct / DungeonClear.RestHealthPct, 0-100, 0 = min(90, AlmostFullHealth)

    static CoaHealSettings const& Get();
};

// "coa heal target": the member a healer walks to, the most hurt below the floor, chosen WITHOUT line of sight and
// biased toward the group's reference tank (Dungeon Clear, DungeonClearHealTargetValue). The stock `party member to
// heal` drops a member the moment it is out of sight, so a healer whose tank was dragged round a corner concluded that
// nobody needed healing and watched it die. Includes the escorted creature (CoaHealEscort), never biased. A GUID only,
// resolved live at use; 200 ms keeps the pick fresh without walking the group every tick.
class CoaHealTargetValue : public CalculatedValue<ObjectGuid>
{
public:
    CoaHealTargetValue(PlayerbotAI* botAI) : CalculatedValue<ObjectGuid>(botAI, "coa heal target", 200) {}

protected:
    ObjectGuid Calculate() override;
};

// `party member to heal`, registered under the stock name (the shared value registry is last-wins and the guards'
// context is added after the stock one): the stock answer, unchanged, and while the group escorts a creature
// (CoaHealEscort) that creature when it is more hurt, so the whole heal rotation keeps it alive with no casting code of
// its own (Dungeon Clear, DungeonClearPartyMemberToHealValue). Off an escort, the stock answer verbatim.
class CoaPartyMemberToHealValue : public PartyMemberToHeal
{
public:
    CoaPartyMemberToHealValue(PlayerbotAI* botAI) : PartyMemberToHeal(botAI, "party member to heal") {}

protected:
    Unit* Calculate() override;
};

// "coa heal reposition": a healer whose heal target is below the floor but out of sight or beyond heal range from
// where it stands (Dungeon Clear, DungeonClearHealRepositionTrigger; decision in CoaHealMath::ShouldReposition).
class CoaHealRepositionTrigger : public Trigger
{
public:
    CoaHealRepositionTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa heal reposition", 1) {}
    bool IsActive() override;
};

// "coa heal reposition": walks the healer to a point in sight and in heal range of its heal target, after which the
// stock heal stack takes the target again (Dungeon Clear, DungeonClearHealRepositionAction).
class CoaHealRepositionAction : public MovementAction
{
public:
    CoaHealRepositionAction(PlayerbotAI* botAI) : MovementAction(botAI, "coa heal reposition") {}
    bool Execute(Event event) override;

private:
    bool FindStandoff(Map* map, Unit* target, float radius, float maxRadius, float& x, float& y, float& z);
    bool PathedCloseOn(Unit* target, float stopShort, float& x, float& y, float& z);

    float lastX = 0.0f;
    float lastY = 0.0f;
    float lastZ = 0.0f;
    bool lastValid = false;
};

// "coa top off party": out of combat, a member within heal range below the between-pulls floor. The kit's only
// out-of-combat heal ("coa group member dropping") waits for 45%, its 65-85% heals live in the combat strategy, so
// after a fight members at 45-85% sat with idle healers until the food (ZG raid, 02/11; Dungeon Clear,
// DungeonClearTopOffPartyTrigger). Drives "coa heal", which picks the target itself and declines above its
// mana-scaled heal line (the healer then drinks); inert for a healer without that action.
class CoaTopOffPartyTrigger : public Trigger
{
public:
    CoaTopOffPartyTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa top off party", 500) {}
    bool IsActive() override;
};

// "coa gardes soins": the healer guards, for the combat and the non-combat engines of every healer.
class CoaGardesSoinsStrategy : public Strategy
{
public:
    CoaGardesSoinsStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}
    std::string const getName() override { return "coa gardes soins"; }
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
};

#endif
