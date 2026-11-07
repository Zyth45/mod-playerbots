/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaGardesContext.h"

#include "CoaDpsTargetValue.h"
#include "CoaGardesBotState.h"
#include "CoaGardesCombat.h"
#include "CoaHazardValues.h"
#include "CoaHazardVacate.h"
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

    // "coa run leader": the run-leader provider's answer for this bot, read at most once per 100 ms (one bot tick),
    // so that the guards' several reads per action candidate take Dungeon Clear's leader-cache lock once.
    class CoaRunLeaderValue : public CalculatedValue<CoaGroupTank::RunLeaderMemo>
    {
    public:
        CoaRunLeaderValue(PlayerbotAI* botAI) : CalculatedValue<CoaGroupTank::RunLeaderMemo>(botAI, "coa run leader", 100)
        {
        }
        CoaGroupTank::RunLeaderMemo Calculate() override { return CoaGroupTank::ReadRunLeader(bot); }
    };
}

CoaGardesValueContext::CoaGardesValueContext()
{
    creators["coa reference tank"] = &reference_tank;
    creators["coa run leader"] = [](PlayerbotAI* ai) -> UntypedValue* { return new CoaRunLeaderValue(ai); };
    creators["coa heal target"] = [](PlayerbotAI* ai) -> UntypedValue* { return new CoaHealTargetValue(ai); };
    creators["party member to heal"] = [](PlayerbotAI* ai) -> UntypedValue* { return new CoaPartyMemberToHealValue(ai); };
    creators["coa tank fight assist state"] = [](PlayerbotAI* ai) -> UntypedValue*
    { return new CoaTankFightAssistStateValue(ai); };
    // Dungeon Clear session's guards (gardes-dc-a, gardes-dc-b).
    creators["coa hazards"] = [](PlayerbotAI* ai) -> UntypedValue* { return new CoaHazardsValue(ai); };
    creators["coa ground hazards"] = [](PlayerbotAI* ai) -> UntypedValue* { return new CoaGroundHazardsValue(ai); };
    creators["coa trap hazards"] = [](PlayerbotAI* ai) -> UntypedValue* { return new CoaTrapHazardsValue(ai); };
    creators["dps target"] = [](PlayerbotAI* ai) -> UntypedValue* { return new CoaDpsTargetValue(ai); };
    creators["dps aoe target"] = [](PlayerbotAI* ai) -> UntypedValue* { return new CoaDpsAoeTargetValue(ai); };
    creators["coa brd state"] = [](PlayerbotAI* ai) -> UntypedValue* { return new CoaBrdStateValue(ai); };
    creators["coa room boss memo"] = [](PlayerbotAI* ai) -> UntypedValue* { return new CoaRoomBossMemoValue(ai); };
    creators["coa no rti"] = [](PlayerbotAI* ai) -> UntypedValue* { return new CoaNoRtiValue(ai); };
}

CoaGardesTriggerContext::CoaGardesTriggerContext()
{
    creators["coa heal reposition"] = [](PlayerbotAI* ai) -> Trigger* { return new CoaHealRepositionTrigger(ai); };
    creators["coa top off party"] = [](PlayerbotAI* ai) -> Trigger* { return new CoaTopOffPartyTrigger(ai); };
    creators["coa tank fight assist"] = [](PlayerbotAI* ai) -> Trigger* { return new CoaTankFightAssistTrigger(ai); };
    creators["coa tank fight assist combat"] = [](PlayerbotAI* ai) -> Trigger*
    { return new CoaTankFightAssistCombatTrigger(ai); };
    // Dungeon Clear session's guards (gardes-dc-a, gardes-dc-b).
    creators["coa hazard vacate"] = [](PlayerbotAI* ai) -> Trigger* { return new CoaHazardVacateTrigger(ai); };
    creators["coa combat regroup"] = [](PlayerbotAI* ai) -> Trigger* { return new CoaCombatRegroupTrigger(ai); };
    creators["coa lyceum close up"] = [](PlayerbotAI* ai) -> Trigger* { return new CoaLyceumCloseUpTrigger(ai); };
    creators["coa iron hall safe spot"] = [](PlayerbotAI* ai) -> Trigger* { return new CoaIronHallSafeSpotTrigger(ai); };
}

CoaGardesActionContext::CoaGardesActionContext()
{
    creators["coa heal reposition"] = [](PlayerbotAI* ai) -> Action* { return new CoaHealRepositionAction(ai); };
    creators["coa tank fight assist"] = [](PlayerbotAI* ai) -> Action*
    { return new CoaTankFightAssistAction(ai, "coa tank fight assist", false); };
    creators["coa tank fight assist combat"] = [](PlayerbotAI* ai) -> Action*
    { return new CoaTankFightAssistAction(ai, "coa tank fight assist combat", true); };
    // Dungeon Clear session's guards (gardes-dc-a, gardes-dc-b).
    creators["coa hazard vacate"] = [](PlayerbotAI* ai) -> Action* { return new CoaHazardVacateAction(ai); };
    creators["coa combat regroup"] = [](PlayerbotAI* ai) -> Action* { return new CoaCombatRegroupAction(ai); };
    creators["coa lyceum close up"] = [](PlayerbotAI* ai) -> Action* { return new CoaLyceumCloseUpAction(ai); };
    creators["coa iron hall safe spot"] = [](PlayerbotAI* ai) -> Action* { return new CoaIronHallSafeSpotAction(ai); };
}

CoaGardesStrategyContext::CoaGardesStrategyContext()
{
    creators["coa gardes soins"] = [](PlayerbotAI* ai) -> Strategy* { return new CoaGardesSoinsStrategy(ai); };
    creators["coa gardes menace"] = [](PlayerbotAI* ai) -> Strategy* { return new CoaGardesMenaceStrategy(ai); };
    creators["coa gardes menace nc"] = [](PlayerbotAI* ai) -> Strategy* { return new CoaGardesMenaceNonCombatStrategy(ai); };
    // Dungeon Clear session's guards (gardes-dc-a, gardes-dc-b).
    creators["coa ground hazards"] = [](PlayerbotAI* ai) -> Strategy* { return new CoaGroundHazardsStrategy(ai); };
    creators["coa target exclusion"] = [](PlayerbotAI* ai) -> Strategy* { return new CoaTargetExclusionStrategy(ai); };
    creators["coa gardes combat"] = [](PlayerbotAI* ai) -> Strategy* { return new CoaGardesCombatStrategy(ai); };
    creators["coa gardes noncombat"] = [](PlayerbotAI* ai) -> Strategy* { return new CoaGardesNonCombatStrategy(ai); };
}
