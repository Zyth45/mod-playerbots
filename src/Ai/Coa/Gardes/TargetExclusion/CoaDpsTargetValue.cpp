/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "CoaDpsTargetValue.h"

#include "CoaGroupTank.h"
#include "CoaTargetExclusion.h"
#include "Player.h"
#include "Playerbots.h"
#include "Unit.h"

namespace
{
    // Did the pick land on something barred for this bot right now? The map test comes first and is the answer on
    // every map without rows, so a pick pays one scan of a small table.
    bool BarredPick(Player* bot, Unit* picked)
    {
        if (!bot || !picked)
            return false;

        uint32 const mapId = bot->GetMapId();
        return CoaTargetExclusion::HasRowsFor(mapId) && CoaGroupTank::ModuleGuardActs(bot, CoaTargetExclusionGuard) &&
               CoaTargetExclusion::IsExcluded(bot, mapId, picked->GetEntry());
    }
}

Unit* CoaDpsTargetValue::Calculate()
{
    Unit* const stock = DpsTargetValue::Calculate();
    // Instances only (2.9.4): the stock pick everywhere else.
    if (!CoaGroupTank::GuardScope(bot) || !BarredPick(bot, stock))
        return stock;

    // The raid icon handed us a creature we may not kill: the same picker with the icon disarmed lands on the adds,
    // or on nothing, which is right when the barred creature is the only thing in the fight.
    Unit* const legal = unmarked.Calculate();
    return BarredPick(bot, legal) ? nullptr : legal;
}

Unit* CoaDpsAoeTargetValue::Calculate()
{
    Unit* const stock = DpsAoeTargetValue::Calculate();
    if (!CoaGroupTank::GuardScope(bot) || !BarredPick(bot, stock))
        return stock;

    Unit* const legal = unmarked.Calculate();
    return BarredPick(bot, legal) ? nullptr : legal;
}

bool CoaTargetExclusionStrategy::HasTargetExclusions() const
{
    // Map-keyed, like Dungeon Clear's: the engine caches this per strategy add/remove, and the instance strategies
    // are added/removed on every map change, so the answer is refreshed exactly when the map changes.
    Player* const self = botAI ? botAI->GetBot() : nullptr;
    return self && CoaTargetExclusion::HasRowsFor(self->GetMapId());
}

void CoaTargetExclusionStrategy::AppendTargetExclusions(GuidSet& exclusions, TargetValueExclusionType type)
{
    // DPS, DEBUFF and AoE pools always; the TANK pool for `alsoTank` rows. The AoE type carries no role, so it is
    // answered from the pool the bot reads for a single target: a tank keeps counting what she may hold.
    Player* const self = botAI ? botAI->GetBot() : nullptr;
    if (!CoaGroupTank::GuardScope(self))
        return;

    bool const forAoe = type == TargetValueExclusionType::Aoe;
    if (!forAoe && type != TargetValueExclusionType::DpsTarget && type != TargetValueExclusionType::DebuffTarget &&
        type != TargetValueExclusionType::TankTarget)
        return;

    uint32 const mapId = self->GetMapId();
    if (!CoaTargetExclusion::HasRowsFor(mapId) || !CoaGroupTank::ModuleGuardActs(self, CoaTargetExclusionGuard))
        return;

    bool const forTank = type == TargetValueExclusionType::TankTarget || (forAoe && PlayerbotAI::IsTank(self));

    AiObjectContext* context = botAI->GetAiObjectContext();
    for (ObjectGuid const guid : AI_VALUE(GuidVector, "attackers"))
    {
        Unit* unit = botAI->GetUnit(guid);
        if (unit && CoaTargetExclusion::IsExcluded(self, mapId, unit->GetEntry(), forTank))
            exclusions.insert(guid);
    }
}
