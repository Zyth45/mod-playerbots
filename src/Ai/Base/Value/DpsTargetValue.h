/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_DPSTARGETVALUE_H
#define PLAYERBOTS_DPSTARGETVALUE_H

#include "PlayerbotAIConfig.h"
#include "RtiTargetValue.h"
#include "Unit.h"

class PlayerbotAI;

class DpsTargetValue : public RtiTargetValue
{
public:
    DpsTargetValue(PlayerbotAI* botAI, std::string const type = "rti", std::string const name = "dps target")
        : RtiTargetValue(botAI, type, name)
    {
        // CoA World (DpsTargetCache): kept for one AI pass (100 ms) instead of being searched again on every call
        // (the trigger "not dps target active", then the action, in the same pass).
        if (sPlayerbotAIConfig.coaDpsTargetCache)
            checkInterval = 100;
    }

    Unit* Calculate() override;

    // CoA World (DpsTargetCache): a kept target that died or left is searched again at once.
    Unit* Get() override
    {
        Unit* target = RtiTargetValue::Get();
        if (target && checkInterval >= 2 && (!target->IsInWorld() || !target->IsAlive()))
        {
            Reset();
            target = RtiTargetValue::Get();
        }
        return target;
    }
};

class DpsAoeTargetValue : public RtiTargetValue
{
public:
    DpsAoeTargetValue(PlayerbotAI* botAI, std::string const type = "rti", std::string const name = "dps aoe target")
        : RtiTargetValue(botAI, type, name)
    {
    }

    Unit* Calculate() override;
};

#endif
