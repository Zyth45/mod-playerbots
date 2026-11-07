/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaGardesContext.h"

#include "CoaGroupTank.h"
#include "Playerbots.h"
#include "Value.h"

namespace
{
    // "coa reference tank": the group's reference tank (CoaGroupTank).
    class CoaReferenceTankValue : public UnitCalculatedValue
    {
    public:
        CoaReferenceTankValue(PlayerbotAI* botAI) : UnitCalculatedValue(botAI, "coa reference tank", 1) {}
        Unit* Calculate() override { return CoaGroupTank::Reference(bot); }
    };

    UntypedValue* reference_tank(PlayerbotAI* botAI) { return new CoaReferenceTankValue(botAI); }
}

CoaGardesValueContext::CoaGardesValueContext()
{
    creators["coa reference tank"] = &reference_tank;
}

CoaGardesTriggerContext::CoaGardesTriggerContext() {}

CoaGardesActionContext::CoaGardesActionContext() {}

CoaGardesStrategyContext::CoaGardesStrategyContext() {}
