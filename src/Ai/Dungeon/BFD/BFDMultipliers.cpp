/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "BFDMultipliers.h"

#include "AiObjectContext.h"
#include "BFDActions.h"
#include "BFDTriggers.h"
#include "MovementActions.h"
#include "Playerbots.h"

using namespace BlackfathomDeeps;

float GhamooraMultiplier::GetValue(Action* action)
{
    if (!dynamic_cast<MovementAction*>(action) || dynamic_cast<DungeonBossMovementAction*>(action) ||
        dynamic_cast<AvoidAoeAction*>(action))
        return 1.0f;

    // While the beam turns, every other move (reaching melee or spell range, following, formation, getting behind,
    // "coa boss dodge") would take a bot off his back.
    Unit* ghamoora = FindGhamoora(botAI, context);
    if (!IsTorrenting(ghamoora) || bot->GetExactDist2d(ghamoora) > TORRENT_REACH + ghamoora->GetCombatReach())
        return 1.0f;

    return 0.0f;
}
