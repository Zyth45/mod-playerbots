/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "StayStrategy.h"
#include "Playerbots.h"

void StayStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // A bot tank's placement ("tank hold", RC11): in combat, the walk back to its point wins over the rotation and
    // the reach (under the emergencies); a ready taunt goes to its given target on the way.
    triggers.push_back(
        new TriggerNode(
            "coa placement away",
            {
                NextAction("coa placement move", ACTION_RAID + 1)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "return to stay position",
            {
                NextAction("return to stay position", ACTION_MOVE)
            }
        )
    );
}

std::vector<NextAction> StayStrategy::getDefaultActions()
{
    return {
        NextAction("stay", 1.0f)
    };
}

void SitStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(
        new TriggerNode(
            "sit",
            {
                NextAction("sit", 1.5f)
            }
        )
    );
}
