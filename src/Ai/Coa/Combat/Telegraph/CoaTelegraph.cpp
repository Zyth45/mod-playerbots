/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaTelegraph.h"

#include "AttackAction.h"
#include "CoaTelegraphRule.h"
#include "DynamicObject.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "ReachTargetActions.h"
#include "SpellAuras.h"

#include <cmath>

DynamicObject* CoaTelegraph::MarkerToLeave(Player* bot, float& safeDistance)
{
    if (!bot || !bot->IsAlive() || !sPlayerbotAIConfig.coaCombatTelegraphEnable)
        return nullptr;

    // The marker's dummy aura is on the bot while it stands inside: one look at its own auras, no grid search.
    for (auto const& [spellId, application] : bot->GetAppliedAuras())
    {
        CoaTelegraphRule::Telegraph const* telegraph = CoaTelegraphRule::Find(spellId);
        if (!telegraph)
            continue;
        Aura* aura = application ? application->GetBase() : nullptr;
        if (!aura || aura->IsRemoved() || aura->GetType() != DYNOBJ_AURA_TYPE)
            continue;
        DynamicObject* marker = aura->GetDynobjOwner();
        if (!marker || !marker->IsInWorld())
            continue;
        float const radius = marker->GetRadius();
        if (!CoaTelegraphRule::MustLeave(*telegraph, radius, bot->GetDistance2d(marker)))
            continue;
        safeDistance = CoaTelegraphRule::SafeDistance(*telegraph, radius);
        return marker;
    }
    return nullptr;
}

bool CoaTelegraphTrigger::IsActive()
{
    float safeDistance = 0.0f;
    return CoaTelegraph::MarkerToLeave(bot, safeDistance) != nullptr;
}

bool CoaTelegraphAction::Execute(Event /*event*/)
{
    float safeDistance = 0.0f;
    DynamicObject* marker = CoaTelegraph::MarkerToLeave(bot, safeDistance);
    if (!marker)
        return false;
    // Straight out from the centre, half a yard past the safe distance; a wall turns the way out by up to 90 degrees.
    // Forced: the hit falls within seconds, and a combat move in flight must not hold the step back.
    float const fallback = bot->GetOrientation() + float(M_PI);
    for (float turn : {0.0f, 0.5f, -0.5f, 1.0f, -1.0f, 1.5708f, -1.5708f})
    {
        float x = 0.0f;
        float y = 0.0f;
        CoaTelegraphRule::ExitPoint(marker->GetPositionX(), marker->GetPositionY(), bot->GetPositionX(),
                                    bot->GetPositionY(), fallback, turn, safeDistance + 0.5f, x, y);
        float z = bot->GetPositionZ();
        if (!bot->GetMap()->CheckCollisionAndGetValidCoords(bot, bot->GetPositionX(), bot->GetPositionY(),
                                                            bot->GetPositionZ(), x, y, z))
            continue;
        if (std::hypot(x - marker->GetPositionX(), y - marker->GetPositionY()) < safeDistance)
            continue;  // the wall stopped it short, still inside
        if (MoveTo(bot->GetMapId(), x, y, z, false, false, true, false, MovementPriority::MOVEMENT_FORCED))
            return true;
    }
    return FleePosition(marker->GetPosition(), safeDistance, 0, MovementPriority::MOVEMENT_FORCED);
}

void CoaTelegraphStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // A one-shot: above every rotation, heal and stock dodge (avoid aoe), with the emergency actions.
    triggers.push_back(new TriggerNode("coa combat telegraph", {NextAction("coa combat telegraph", ACTION_EMERGENCY)}));
}

void CoaTelegraphStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new CoaTelegraphMeleeMultiplier(botAI));
}

DynamicObject* CoaTelegraph::MarkerUnderCaster(Unit* target, float& safeDistance)
{
    if (!target || !target->IsAlive() || !sPlayerbotAIConfig.coaCombatTelegraphEnable)
        return nullptr;
    for (CoaTelegraphRule::Telegraph const& telegraph : CoaTelegraphRule::TELEGRAPHS)
    {
        DynamicObject* marker = target->GetDynObject(telegraph.spellId);
        if (!marker || !marker->IsInWorld())
            continue;
        float const safe = CoaTelegraphRule::SafeDistance(telegraph, marker->GetRadius());
        if (target->GetDistance2d(marker) >= safe)
            continue;
        safeDistance = safe;
        return marker;
    }
    return nullptr;
}

float CoaTelegraphMeleeMultiplier::GetValue(Action* action)
{
    if (!sPlayerbotAIConfig.coaCombatTelegraphEnable || PlayerbotAI::IsTank(bot))
        return 1.0f;
    if (!dynamic_cast<ReachMeleeAction*>(action) && !dynamic_cast<MeleeAction*>(action))
        return 1.0f;
    float safeDistance = 0.0f;
    return CoaTelegraph::MarkerUnderCaster(AI_VALUE(Unit*, "current target"), safeDistance) ? 0.0f : 1.0f;
}
