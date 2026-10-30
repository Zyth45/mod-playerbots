/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "VaultsMultipliers.h"
#include "MovementActions.h"
#include "Playerbots.h"
#include "ReachTargetActions.h"
#include "VaultsActions.h"
#include "VaultsShared.h"

using namespace VaultsShared;

float VaultsHazardMultiplier::GetValue(Action* action)
{
    if (dynamic_cast<VaultsLeaveHazardAction*>(action) || dynamic_cast<VaultsSoakAction*>(action) ||
        dynamic_cast<VaultsTakeShelterAction*>(action) || dynamic_cast<VaultsClickCrystalAction*>(action))
        return 1.0f;

    bool const isMovement =
        dynamic_cast<MovementAction*>(action) || dynamic_cast<CastReachTargetSpellAction*>(action);
    if (!isMovement || bot->GetMapId() != VAULTS_MAP_ID)
        return 1.0f;

    // Standing in a hazard, the only way to go is out of it. Out of it, a bot does not walk back in to reach its
    // target; it waits until the hazard is gone. In the soak or the shelter, or while breaking a seal, a bot
    // stays put, and on its way there nothing else moves it. A healer in the shelter has the group in range
    // anyway: they all stand in it.
    if (InHazard(bot) || WayBlocked(bot) || Holding(bot) || SoakToTake(bot) || ShelterToReach(bot))
        return 0.0f;

    return 1.0f;
}
