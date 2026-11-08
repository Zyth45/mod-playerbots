/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "CoaTargetExclusion.h"

#include "Creature.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "ObjectAccessor.h"
#include "Player.h"

#include <list>

namespace
{
    CoaTargetExclusion::RunHooks Hooks;

    // Blackwing Lair: Razorgore, for as long as an egg is standing.
    bool RazorgoreEggPhase(Player* bot)
    {
        if (!bot)
            return false;

        namespace Bwl = CoaTargetExclusion::Bwl;
        if (InstanceScript* inst = bot->GetInstanceScript())
            return CoaTargetExclusion::RazorgoreEggPhase(true, inst->GetData(Bwl::DATA_EGG_EVENT) == DONE, false);

        std::list<GameObject*> eggs;
        bot->GetGameObjectListWithEntryInGrid(eggs, Bwl::GO_BLACK_DRAGON_EGG, Bwl::ROOM_SCAN);
        bool standing = false;
        for (GameObject* egg : eggs)
            if (egg && egg->isSpawned() && egg->GetGoState() == GO_STATE_READY)
            {
                standing = true;
                break;
            }
        return CoaTargetExclusion::RazorgoreEggPhase(false, false, standing);
    }

    // Blackwing Lair: the drake hall bosses, while Broodlord stands (and the operator did not skip him).
    bool BwlDrakeHallOutOfOrder(Player* bot)
    {
        if (!bot)
            return false;

        namespace Bwl = CoaTargetExclusion::Bwl;
        bool const skipped = Hooks.bossSkipped && Hooks.bossSkipped(bot, Bwl::NPC_BROODLORD_LASHLAYER);
        InstanceScript* inst = bot->GetInstanceScript();
        return CoaTargetExclusion::BwlDrakeHallOutOfOrder(
            skipped, inst != nullptr, inst && inst->GetBossState(Bwl::BROODLORD_ENCOUNTER_INDEX) == DONE);
    }

    // Gundrak: a Drakkari Raider a combat purge has just dropped (only a Dungeon Clear run arms one).
    bool GundrakRaiderJustPurged(Player* bot)
    {
        return Hooks.purgeBarred && Hooks.purgeBarred(bot, CoaTargetExclusion::Gundrak::NPC_DRAKKARI_RAIDER);
    }

    // Halls of Reflection: the Lich King, for the whole of the escape.
    bool HorEscapeRunning(Player* bot)
    {
        namespace Hor = CoaTargetExclusion::HallsOfReflection;
        if (!bot || bot->GetMapId() != Hor::MAP_ID)
            return false;
        InstanceScript* inst = bot->GetInstanceScript();
        return CoaTargetExclusion::HorEscapeRunning(true, inst != nullptr,
                                                    inst && inst->GetBossState(Hor::DATA_LICH_KING) == IN_PROGRESS);
    }

    // Trial of the Champion: Paletress, while her Memory lives (Reflective Shield up).
    bool PaletressShielded(Player* bot)
    {
        namespace Toc = CoaTargetExclusion::TrialOfTheChampion;
        if (!bot || bot->GetMapId() != Toc::MAP_ID)
            return false;
        InstanceScript* inst = bot->GetInstanceScript();
        if (!inst)
            return false;
        Creature* boss = ObjectAccessor::GetCreature(*bot, inst->GetGuidData(Toc::DATA_ARGENT_CHAMPION));
        return CoaTargetExclusion::PaletressShielded(true, boss && boss->IsAlive(),
                                                     boss && boss->GetEntry() == Toc::NPC_PALETRESS,
                                                     boss && boss->HasAura(Toc::SPELL_REFLECTIVE_SHIELD));
    }

    bool InForce(Player* bot, CoaTargetExclusion::Gate gate)
    {
        using Gate = CoaTargetExclusion::Gate;
        switch (gate)
        {
            case Gate::Always:
                return true;
            case Gate::RazorgoreEggPhase:
                return RazorgoreEggPhase(bot);
            case Gate::BwlDrakeHallOutOfOrder:
                return BwlDrakeHallOutOfOrder(bot);
            case Gate::GundrakRaiderJustPurged:
                return GundrakRaiderJustPurged(bot);
            case Gate::HorEscapeRunning:
                return HorEscapeRunning(bot);
            case Gate::PaletressShielded:
                return PaletressShielded(bot);
        }
        return false;
    }
}

void CoaTargetExclusion::SetRunHooks(RunHooks hooks)
{
    Hooks = std::move(hooks);
}

bool CoaTargetExclusion::IsExcluded(Player* bot, uint32 mapId, uint32 entry, bool forTank)
{
    return Decide(mapId, entry, forTank, [bot](Gate gate) { return InForce(bot, gate); });
}
