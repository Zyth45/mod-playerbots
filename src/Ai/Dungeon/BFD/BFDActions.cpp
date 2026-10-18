/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "BFDActions.h"

#include "AiObjectContext.h"
#include "Playerbots.h"

#include <algorithm>

using namespace BlackfathomDeeps;
using namespace DungeonBoss;

bool BfdMoveFromTorrentAction::Execute(Event /*event*/)
{
    Unit* ghamoora = FindGhamoora(botAI, context);
    if (!IsTorrenting(ghamoora) || !OffHisBack(ghamoora, bot))
        return false;

    // Round the nearer side towards his back; out of the beam itself by the side it has already swept.
    float const angle = RelativeAngle(ghamoora, bot);
    bool const clockwise = angle < -TORRENT_HALF_ARC;
    float const target = clockwise ? std::max(angle - TORRENT_STEP, -float(M_PI))
                                   : std::min(angle + TORRENT_STEP, float(M_PI));

    // Melee stay in reach of his back to keep hitting him; he does not fight back while he turns.
    float const closest = ghamoora->GetCombatReach() + 1.0f;
    float const distance = botAI->IsRanged(bot)
        ? std::clamp(bot->GetExactDist2d(ghamoora), closest + 2.0f, TORRENT_RANGED_DISTANCE)
        : closest;
    return MoveAround(ghamoora, target, distance);
}

bool BfdMoveFromFrontalConeAction::Execute(Event /*event*/)
{
    FrontalCone const* cone = nullptr;
    Unit* boss = ConeOver(botAI, context, bot, FRONTAL_CONES, cone);
    return boss && MoveOutOfCone(boss, *cone);
}
