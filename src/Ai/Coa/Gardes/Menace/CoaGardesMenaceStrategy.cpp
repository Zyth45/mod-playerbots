/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaGardesMenaceStrategy.h"

#include "AttackAction.h"
#include "CoaGroupTank.h"
#include "CoaThreatLead.h"
#include "GenericSpellActions.h"
#include "Playerbots.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

namespace
{
    // Dungeon Clear's relevances: above the stock reach actions (20) in the combat engine, above follow in the
    // non-combat one (DcRelevance AssistCampCombat 35, AssistCamp 29).
    constexpr float AssistCombatRelevance = 35.0f;
    constexpr float AssistNonCombatRelevance = 29.0f;

    // Does `action` deal damage? Attacks, the stock target picker that starts them, and any harmful spell, judged by
    // the spell, not by its target: Frost Nova, Arcane Explosion and Consecration are cast on the caster and still hit
    // the pack. Heals and buffs never.
    bool IsDamagingAction(PlayerbotAI* botAI, Action* action, std::string const& name)
    {
        if (name == "dps assist" || dynamic_cast<AttackAction*>(action))
            return true;
        if (dynamic_cast<CastHealingSpellAction*>(action) || dynamic_cast<CastBuffSpellAction*>(action))
            return false;
        CastSpellAction* cast = dynamic_cast<CastSpellAction*>(action);
        if (!cast)
            return false;
        uint32 const spellId = botAI->GetAiObjectContext()->GetValue<uint32>("spell id", cast->getSpell())->Get();
        SpellInfo const* info = spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
        return info && !info->IsPositive();
    }

    // A mob already beating on a non-tank member of the group is peeled at once: the lead protects the tank's grip on
    // HIS mobs, and waiting it out while a caster eats a mob only leaves that mob alone. Single-target actions only.
    bool PeelsAMobOffTheParty(Player* bot, PlayerbotAI* botAI, Action* action)
    {
        Unit* target = nullptr;
        if (CastSpellAction* cast = dynamic_cast<CastSpellAction*>(action))
            target = cast->GetTarget();
        else if (dynamic_cast<AttackAction*>(action))
            target = botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
        if (!target || target == bot)
            return false;
        Unit* victim = target->GetVictim();
        Player* p = victim ? victim->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
        return p && p != bot && p->IsInSameRaidWith(bot) && !PlayerbotAI::IsTank(p);
    }
}

float CoaGardesMenaceMultiplier::GetValue(Action* action)
{
    // Instances only (2.9.4): stock everywhere else, before any value read.
    if (!action || !botAI || !CoaGroupTank::GuardScope(bot))
        return 1.0f;
    std::string const& name = action->getName();

    // The threat lead, damage side: a damage dealer holds all damage for the lead after the tank's first hit.
    // Movement is untouched (the assist still closes the gap). Only damaging actions pay for the tank lookup.
    if (IsDamagingAction(botAI, action, name) && CoaThreatLead::ShouldHoldDpsDamage(bot) &&
        !PeelsAMobOffTheParty(bot, botAI, action))
        return 0.0f;

    if (name != "drop target")
        return 1.0f;

    // "drop target" (relevance 99) treats a target out of sight as invalid and leaves the combat engine: a bot the
    // assist put on a mob of the tank's fight around a corner would bounce back to the non-combat engine every tick
    // before it could move (Dungeon Clear: 1679 engine flips, 0 engages). Kept only for that case: a damage dealer,
    // the assist wanted, and a target alive, attackable and merely out of sight. A dead or truly invalid target still
    // drops.
    if (PlayerbotAI::IsHeal(bot) || !CoaThreatLead::IsTankFightAssistWanted(bot))
        return 1.0f;
    Unit* target = AI_VALUE(Unit*, "current target");
    if (target && target->IsAlive() && target->FindMap() == bot->FindMap() && bot->IsValidAttackTarget(target) &&
        !bot->IsWithinLOSInMap(target))
        return 0.0f;
    return 1.0f;
}

void CoaGardesMenaceStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("coa tank fight assist combat",
                                       {NextAction("coa tank fight assist combat", AssistCombatRelevance)}));
}

void CoaGardesMenaceStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new CoaGardesMenaceMultiplier(botAI));
}

void CoaGardesMenaceNonCombatStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(
        new TriggerNode("coa tank fight assist", {NextAction("coa tank fight assist", AssistNonCombatRelevance)}));
}

void CoaGardesMenaceNonCombatStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new CoaGardesMenaceMultiplier(botAI));
}
