/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "DpsTargetValue.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "Strategy.h"

namespace
{
    float StickyLifeTime(Unit* unit, Unit* currentTarget, float lifeTime)
    {
        return sPlayerbotAIConfig.coaStickyTarget && unit == currentTarget ? lifeTime * 0.75f : lifeTime;
    }

    float StickyDistance(Player* bot, Unit* unit, Unit* currentTarget)
    {
        float const distance = bot->GetDistance(unit);
        return sPlayerbotAIConfig.coaStickyTarget && unit == currentTarget ? distance - 5.0f : distance;
    }
}  // namespace

class FindMaxThreatGapTargetStrategy : public FindTargetStrategy
{
public:
    FindMaxThreatGapTargetStrategy(PlayerbotAI* botAI) : FindTargetStrategy(botAI), minThreat(0) {}

    void CheckAttacker(Unit* attacker, ThreatManager* threatMgr) override
    {
        if (!attacker->IsAlive())
            return;

        if (CheckPriority(attacker))
            return;

        if (!result || CalcThreatGap(attacker, threatMgr) > CalcThreatGap(result, &result->GetThreatMgr()))
            result = attacker;
    }
    float CalcThreatGap(Unit* attacker, ThreatManager* threatMgr)
    {
        Unit* victim = attacker->GetVictim();
        return threatMgr->GetThreat(victim) - threatMgr->GetThreat(attacker);
    }

protected:
    float minThreat;
};

// caster
class CasterFindTargetSmartStrategy : public FindTargetStrategy
{
public:
    CasterFindTargetSmartStrategy(PlayerbotAI* botAI, float dps)
        : FindTargetStrategy(botAI), dps_(dps), targetExpectedLifeTime(1000000),
          currentTarget(botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get())
    {
        result = nullptr;
    }

    void CheckAttacker(Unit* attacker, ThreatManager* /*threatMgr*/) override
    {
        if (Group* group = botAI->GetBot()->GetGroup())
        {
            ObjectGuid guid = group->GetTargetIcon(4);
            if (guid && attacker->GetGUID() == guid)
                return;
        }
        if (!attacker->IsAlive())
            return;

        if (CheckPriority(attacker))
            return;

        float expectedLifeTime = attacker->GetHealth() / dps_;
        // Unit* victim = attacker->GetVictim();
        if (!result || IsBetter(attacker, result))
        {
            targetExpectedLifeTime = expectedLifeTime;
            result = attacker;
        }
    }
    bool IsBetter(Unit* new_unit, Unit* old_unit)
    {
        float new_time = new_unit->GetHealth() / dps_;
        float old_time = old_unit->GetHealth() / dps_;
        // [5-30] > (5-0] > (20-inf)
        int new_level = GetIntervalLevel(new_unit);
        int old_level = GetIntervalLevel(old_unit);
        if (new_level != old_level)
            return new_level > old_level;

        int32_t level = new_level;
        if (level % 10 == 2 || level % 10 == 0)
            return StickyLifeTime(new_unit, currentTarget, new_time) <
                   StickyLifeTime(old_unit, currentTarget, old_time);
        // dont switch targets when all of them with low health
        if (currentTarget == new_unit)
            return true;

        if (currentTarget == old_unit)
            return false;

        return new_time > old_time;
    }
    int32_t GetIntervalLevel(Unit* unit)
    {
        float time = unit->GetHealth() / dps_;
        float dis = unit->GetDistance(botAI->GetBot());
        float attackRange =
            botAI->IsRanged(botAI->GetBot()) ? sPlayerbotAIConfig.spellDistance : sPlayerbotAIConfig.meleeDistance;
        attackRange += 5.0f;
        if (sPlayerbotAIConfig.coaStickyTarget && unit == currentTarget)
            attackRange += 5.0f;
        int level = dis < attackRange ? 10 : 0;
        if (time >= 5 && time <= 30)
            return level + 2;

        if (time > 30)
            return level;

        return level + 1;
    }

protected:
    float dps_;
    float targetExpectedLifeTime;
    Unit* currentTarget;
};

// General
class GeneralFindTargetSmartStrategy : public FindTargetStrategy
{
public:
    GeneralFindTargetSmartStrategy(PlayerbotAI* botAI, float dps)
        : FindTargetStrategy(botAI), dps_(dps), targetExpectedLifeTime(1000000),
          currentTarget(botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get())
    {
    }

    void CheckAttacker(Unit* attacker, ThreatManager*) override
    {
        if (Group* group = botAI->GetBot()->GetGroup())
        {
            ObjectGuid guid = group->GetTargetIcon(4);
            if (guid && attacker->GetGUID() == guid)
                return;
        }
        if (!attacker->IsAlive())
            return;

        if (CheckPriority(attacker))
            return;

        float expectedLifeTime = attacker->GetHealth() / dps_;
        // Unit* victim = attacker->GetVictim();
        if (!result || IsBetter(attacker, result))
        {
            targetExpectedLifeTime = expectedLifeTime;
            result = attacker;
        }
    }
    bool IsBetter(Unit* new_unit, Unit* old_unit)
    {
        float new_time = new_unit->GetHealth() / dps_;
        float old_time = old_unit->GetHealth() / dps_;
        int new_level = GetIntervalLevel(new_unit);
        int old_level = GetIntervalLevel(old_unit);
        if (new_level != old_level)
        {
            return new_level > old_level;
        }
        // attack enemy in range and with lowest health
        int level = new_level;
        if (level == 10)
            return StickyLifeTime(new_unit, currentTarget, new_time) <
                   StickyLifeTime(old_unit, currentTarget, old_time);

        // all targets are far away, choose the closest one
        return StickyDistance(botAI->GetBot(), new_unit, currentTarget) <
               StickyDistance(botAI->GetBot(), old_unit, currentTarget);
    }
    int32_t GetIntervalLevel(Unit* unit)
    {
        float dis = unit->GetDistance(botAI->GetBot());
        float attackRange =
            botAI->IsRanged(botAI->GetBot()) ? sPlayerbotAIConfig.spellDistance : sPlayerbotAIConfig.meleeDistance;
        attackRange += 5.0f;
        if (sPlayerbotAIConfig.coaStickyTarget && unit == currentTarget)
            attackRange += 5.0f;
        int level = dis < attackRange ? 10 : 0;
        return level;
    }

protected:
    float dps_;
    float targetExpectedLifeTime;
    Unit* currentTarget;
};

// combo
class ComboFindTargetSmartStrategy : public FindTargetStrategy
{
public:
    ComboFindTargetSmartStrategy(PlayerbotAI* botAI, float dps)
        : FindTargetStrategy(botAI), dps_(dps), targetExpectedLifeTime(1000000),
          currentTarget(botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get())
    {
    }

    void CheckAttacker(Unit* attacker, ThreatManager*) override
    {
        if (Group* group = botAI->GetBot()->GetGroup())
        {
            ObjectGuid guid = group->GetTargetIcon(4);
            if (guid && attacker->GetGUID() == guid)
                return;
        }
        if (!attacker->IsAlive())
            return;

        if (CheckPriority(attacker))
            return;

        float expectedLifeTime = attacker->GetHealth() / dps_;
        // Unit* victim = attacker->GetVictim();
        if (!result || IsBetter(attacker, result))
        {
            targetExpectedLifeTime = expectedLifeTime;
            result = attacker;
        }
    }
    bool IsBetter(Unit* new_unit, Unit* old_unit)
    {
        float new_time = new_unit->GetHealth() / dps_;
        float old_time = old_unit->GetHealth() / dps_;
        // [5-20] > (5-0] > (20-inf)
        int new_level = GetIntervalLevel(new_unit);
        int old_level = GetIntervalLevel(old_unit);
        if (new_level != old_level)
            return new_level > old_level;

        // attack enemy in range and with lowest health
        int level = new_level;
        Player* bot = botAI->GetBot();
        if (level == 10)
        {
            Unit* combo_unit = bot->GetComboTarget();
            if (new_unit == combo_unit)
                return true;

            return StickyLifeTime(new_unit, currentTarget, new_time) <
                   StickyLifeTime(old_unit, currentTarget, old_time);
        }
        // all targets are far away, choose the closest one
        return StickyDistance(bot, new_unit, currentTarget) < StickyDistance(bot, old_unit, currentTarget);
    }
    int32_t GetIntervalLevel(Unit* unit)
    {
        float dis = unit->GetDistance(botAI->GetBot());
        float attackRange =
            botAI->IsRanged(botAI->GetBot()) ? sPlayerbotAIConfig.spellDistance : sPlayerbotAIConfig.meleeDistance;
        attackRange += 5.0f;
        if (sPlayerbotAIConfig.coaStickyTarget && unit == currentTarget)
            attackRange += 5.0f;
        int level = dis < attackRange ? 10 : 0;
        return level;
    }

protected:
    float dps_;
    float targetExpectedLifeTime;
    Unit* currentTarget;
};

Unit* DpsTargetValue::Calculate()
{
    GuidSet const exclusions = GatherStrategyTargetExclusions(botAI, TargetValueExclusionType::DpsTarget);
    Unit* rti = RtiTargetValue::Calculate();
    if (rti && (!sPlayerbotAIConfig.coaExclusionsEverywhere || exclusions.find(rti->GetGUID()) == exclusions.end()))
        return rti;

    float dps = AI_VALUE(float, "estimated group dps");

    if (botAI->GetNearGroupMemberCount() > 3)
    {
        if (botAI->IsCaster(bot))
        {
            // Caster find target strategy avoids casting spells on enemies
            // with too low health to ensure the effectiveness of casting
            CasterFindTargetSmartStrategy strategy(botAI, dps);
            return TargetValue::FindTarget(&strategy, exclusions);
        }
        else if (botAI->IsCombo(bot))
        {
            ComboFindTargetSmartStrategy strategy(botAI, dps);
            return TargetValue::FindTarget(&strategy, exclusions);
        }
    }
    GeneralFindTargetSmartStrategy strategy(botAI, dps);
    return TargetValue::FindTarget(&strategy, exclusions);
}

class FindMaxHpTargetStrategy : public FindTargetStrategy
{
public:
    FindMaxHpTargetStrategy(PlayerbotAI* botAI) : FindTargetStrategy(botAI), maxHealth(0) {}

    void CheckAttacker(Unit* attacker, ThreatManager*) override
    {
        if (Group* group = botAI->GetBot()->GetGroup())
        {
            ObjectGuid guid = group->GetTargetIcon(4);
            if (guid && attacker->GetGUID() == guid)
                return;
        }

        if (!result || result->GetHealth() < attacker->GetHealth())
            result = attacker;
    }

protected:
    float maxHealth;
};

Unit* DpsAoeTargetValue::Calculate()
{
    GuidSet const exclusions = GatherStrategyTargetExclusions(botAI, TargetValueExclusionType::DpsTarget);
    Unit* rti = RtiTargetValue::Calculate();
    if (rti && (!sPlayerbotAIConfig.coaExclusionsEverywhere || exclusions.find(rti->GetGUID()) == exclusions.end()))
        return rti;

    FindMaxHpTargetStrategy strategy(botAI);
    return TargetValue::FindTarget(&strategy, exclusions);
}
