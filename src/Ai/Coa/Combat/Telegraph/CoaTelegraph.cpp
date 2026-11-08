/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaTelegraph.h"

#include "CoaTelegraphRule.h"
#include "DynamicObject.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "SpellAuras.h"

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
    // Forced, and no repeat floor: the hit falls within seconds, and a combat move in flight must not hold the step
    // back.
    return FleePosition(marker->GetPosition(), safeDistance, 0, MovementPriority::MOVEMENT_FORCED);
}

void CoaTelegraphStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // A one-shot: above every rotation, heal and stock dodge (avoid aoe), with the emergency actions.
    triggers.push_back(new TriggerNode("coa combat telegraph", {NextAction("coa combat telegraph", ACTION_EMERGENCY)}));
}
