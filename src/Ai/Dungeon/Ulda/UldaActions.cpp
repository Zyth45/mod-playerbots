/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "UldaActions.h"

#include "AiObjectContext.h"
#include "Playerbots.h"

using namespace Uldaman;

bool UldaMoveFromGroundTremorAction::Execute(Event /*event*/)
{
    Unit* archaedas = ArchaedasNear(botAI, context, bot);
    return archaedas && KeepAway(archaedas, GROUND_TREMOR_RADIUS);
}
