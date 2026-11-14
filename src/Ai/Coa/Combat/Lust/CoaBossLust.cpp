/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaBossLust.h"

#include "Creature.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "ShamanActions.h"

namespace
{
    bool IsBoss(Unit* unit)
    {
        Creature* creature = unit ? unit->ToCreature() : nullptr;
        return creature && creature->IsAlive() && creature->IsInCombat() &&
               (creature->IsDungeonBoss() || creature->isWorldBoss());
    }
}

CoaBossLustRule::Verdict CoaBossLust::Verdict(PlayerbotAI* botAI)
{
    Player* bot = botAI->GetBot();
    CoaBossLustRule::Situation s;
    s.enabled = sPlayerbotAIConfig.coaCombatLustBossOnly;
    s.grouped = bot->GetGroup() != nullptr;
    s.thresholdPct = sPlayerbotAIConfig.coaCombatLustBossHealthPct;
    if (!s.enabled || !s.grouped)
        return CoaBossLustRule::Decide(s);

    for (char const* const* name = CoaBossLustRule::EncounterStrategies(); *name; ++name)
        if (botAI->HasStrategy(*name, BOT_STATE_COMBAT))
        {
            s.encounterDecides = true;
            return CoaBossLustRule::Decide(s);
        }

    // The boss closest to its end among the attackers and the bot's target.
    AiObjectContext* context = botAI->GetAiObjectContext();
    Unit* boss = nullptr;
    auto consider = [&](Unit* unit)
    {
        if (IsBoss(unit) && unit->FindMap() == bot->FindMap() && (!boss || unit->GetHealthPct() < boss->GetHealthPct()))
            boss = unit;
    };
    consider(context->GetValue<Unit*>("current target")->Get());
    for (ObjectGuid const& guid : context->GetValue<GuidVector>("attackers")->Get())
        consider(botAI->GetUnit(guid));

    if (boss)
    {
        s.bossInFight = true;
        s.bossHealthPct = boss->GetHealthPct();
        s.lateBurstPct = CoaBossLustRule::LateBurstPct(boss->GetName());
    }
    return CoaBossLustRule::Decide(s);
}

bool CoaBossLustTrigger::IsActive()
{
    // A player who turned the boost strategy off keeps it off.
    if (!botAI->HasStrategy("boost", BOT_STATE_COMBAT) || !BuffTrigger::IsActive())
        return false;
    return CoaBossLust::Verdict(botAI) == CoaBossLustRule::Verdict::Allow;
}

float CoaBossLustMultiplier::GetValue(Action* action)
{
    if (!action || !(dynamic_cast<CastBloodlustAction*>(action) || dynamic_cast<CastHeroismAction*>(action)))
        return 1.0f;
    return CoaBossLust::Verdict(botAI) == CoaBossLustRule::Verdict::Forbid ? 0.0f : 1.0f;
}

void CoaBossLustStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("coa combat boss lust heroism", { NextAction("heroism", 30.0f) }));
    triggers.push_back(new TriggerNode("coa combat boss lust bloodlust", { NextAction("bloodlust", 30.0f) }));
}

void CoaBossLustStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new CoaBossLustMultiplier(botAI));
}
