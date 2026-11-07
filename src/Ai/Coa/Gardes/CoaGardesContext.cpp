/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaGardesContext.h"

#include "CoaGardesMenaceStrategy.h"
#include "CoaGardesSoins.h"
#include "CoaGroupTank.h"
#include "CoaTankFightAssist.h"
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
    creators["coa heal target"] = [](PlayerbotAI* ai) -> UntypedValue* { return new CoaHealTargetValue(ai); };
    creators["party member to heal"] = [](PlayerbotAI* ai) -> UntypedValue* { return new CoaPartyMemberToHealValue(ai); };
    creators["coa tank fight assist state"] = [](PlayerbotAI* ai) -> UntypedValue*
    { return new CoaTankFightAssistStateValue(ai); };
}

CoaGardesTriggerContext::CoaGardesTriggerContext()
{
    creators["coa heal reposition"] = [](PlayerbotAI* ai) -> Trigger* { return new CoaHealRepositionTrigger(ai); };
    creators["coa top off party"] = [](PlayerbotAI* ai) -> Trigger* { return new CoaTopOffPartyTrigger(ai); };
    creators["coa tank fight assist"] = [](PlayerbotAI* ai) -> Trigger* { return new CoaTankFightAssistTrigger(ai); };
    creators["coa tank fight assist combat"] = [](PlayerbotAI* ai) -> Trigger*
    { return new CoaTankFightAssistCombatTrigger(ai); };
}

CoaGardesActionContext::CoaGardesActionContext()
{
    creators["coa heal reposition"] = [](PlayerbotAI* ai) -> Action* { return new CoaHealRepositionAction(ai); };
    creators["coa tank fight assist"] = [](PlayerbotAI* ai) -> Action*
    { return new CoaTankFightAssistAction(ai, "coa tank fight assist", false); };
    creators["coa tank fight assist combat"] = [](PlayerbotAI* ai) -> Action*
    { return new CoaTankFightAssistAction(ai, "coa tank fight assist combat", true); };
}

CoaGardesStrategyContext::CoaGardesStrategyContext()
{
    creators["coa gardes soins"] = [](PlayerbotAI* ai) -> Strategy* { return new CoaGardesSoinsStrategy(ai); };
    creators["coa gardes menace"] = [](PlayerbotAI* ai) -> Strategy* { return new CoaGardesMenaceStrategy(ai); };
    creators["coa gardes menace nc"] = [](PlayerbotAI* ai) -> Strategy* { return new CoaGardesMenaceNonCombatStrategy(ai); };
}
