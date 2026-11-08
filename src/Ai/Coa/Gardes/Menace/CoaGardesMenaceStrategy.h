/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef COA_GARDES_MENACE_STRATEGY_H
#define COA_GARDES_MENACE_STRATEGY_H

#include "Strategy.h"

class PlayerbotAI;

// The threat lead on every action that deals damage (stock rotation, melee, "dps assist", class reflexes, a Frost
// Nova cast on the caster), and the "drop target" guard of the assist on the tank's fight: a bot closing on a mob of
// the tank's fight that is out of its sight must not drop it and leave the combat engine (Dungeon Clear's
// DungeonClear{,Combat}Multiplier, 07/11; only these two rules came over).
class CoaGardesMenaceMultiplier : public Multiplier
{
public:
    CoaGardesMenaceMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "coa gardes menace") {}
    float GetValue(Action* action) override;
};

// "coa gardes menace", combat engine: the multiplier and the combat side of the assist on the tank's fight.
class CoaGardesMenaceStrategy : public Strategy
{
public:
    CoaGardesMenaceStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}

    std::string const getName() override { return "coa gardes menace"; }
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    void InitMultipliers(std::vector<Multiplier*>& multipliers) override;
};

// "coa gardes menace nc", non-combat engine: the multiplier (it holds the opener) and the non-combat side of the
// assist.
class CoaGardesMenaceNonCombatStrategy : public Strategy
{
public:
    CoaGardesMenaceNonCombatStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}

    std::string const getName() override { return "coa gardes menace nc"; }
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    void InitMultipliers(std::vector<Multiplier*>& multipliers) override;
};

#endif
