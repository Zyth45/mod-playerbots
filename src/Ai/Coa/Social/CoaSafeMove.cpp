/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaSafeMove.h"

#include "CoaSafeMoveRules.h"
#include "Log.h"
#include "MotionMaster.h"
#include "PathGenerator.h"
#include "Player.h"

static_assert(CoaSafeMove::NORMAL == PATHFIND_NORMAL && CoaSafeMove::SHORTCUT == PATHFIND_SHORTCUT &&
                  CoaSafeMove::INCOMPLETE == PATHFIND_INCOMPLETE && CoaSafeMove::NOPATH == PATHFIND_NOPATH &&
                  CoaSafeMove::NOT_USING_PATH == PATHFIND_NOT_USING_PATH && CoaSafeMove::SHORT == PATHFIND_SHORT &&
                  CoaSafeMove::FARFROMPOLY_START == PATHFIND_FARFROMPOLY_START &&
                  CoaSafeMove::FARFROMPOLY_END == PATHFIND_FARFROMPOLY_END,
              "CoaSafeMove flags must match the core's PathType");

namespace CoaSafeMove
{
bool MovePoint(Player* bot, float x, float y, float z, bool allowSwim)
{
    if (!bot || !bot->IsInWorld())
        return false;
    PathGenerator path(bot);
    if (!path.CalculatePath(x, y, z, false))
        return false;
    bool const waterLine = allowSwim && bot->IsInWater() && bot->IsWithinLOS(x, y, z);
    if (!Walkable(uint32(path.GetPathType()), waterLine))
    {
        // Bench: the moves refused (the core would have gone through a wall), with log level debug.
        LOG_DEBUG("playerbots", "coa safe move refused: bot {} map {} from {:.1f} {:.1f} {:.1f} to {:.1f} {:.1f} {:.1f} type {}",
                  bot->GetName(), bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), x, y,
                  z, uint32(path.GetPathType()));
        return false;
    }
    bot->GetMotionMaster()->MovePoint(0, x, y, z, FORCED_MOVEMENT_NONE, 0.0f, 0.0f, true, false);
    return true;
}
bool CanWalkTo(Player* bot, float x, float y, float z)
{
    if (!bot || !bot->IsInWorld())
        return false;
    // As the core will follow it (CoreStraightLine): a cut path is fine, only the straight line is refused. Refusing
    // every cut path (3.1.6 lot 4) trapped bots inside the bank: from in there, a long path is cut at the point limit.
    PathGenerator path(bot);
    return path.CalculatePath(x, y, z, false) &&
           !CoreStraightLine(uint32(path.GetPathType()), uint32(path.GetPath().size()));
}
}  // namespace CoaSafeMove
