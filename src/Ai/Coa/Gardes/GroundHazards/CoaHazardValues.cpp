/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaHazardValues.h"

#include "CoaGroupTank.h"

#include "CellImpl.h"
#include "CoaHazardRegistry.h"
#include "DynamicObject.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"

namespace
{
    float HazardRange() { return sPlayerbotAIConfig.sightDistance; }

    struct RegisteredHazardInRangeCheck
    {
        RegisteredHazardInRangeCheck(WorldObject const* source, float range) : _source(source), _range(range) {}

        bool operator()(Unit* u) const
        {
            return u && u->IsCreature() && CoaHazardRegistry::Find(u->GetMapId(), u->GetEntry()) != nullptr &&
                   u->IsAlive() && _source->IsWithinDistInMap(u, _range);
        }

        WorldObject const* _source;
        float _range;
    };

    struct AnyDynamicObjectInRangeCheck
    {
        AnyDynamicObjectInRangeCheck(WorldObject const* source, float range) : _source(source), _range(range) {}

        bool operator()(WorldObject* obj) const
        {
            return obj && _source->IsWithinDist(obj, _range, /*is3D*/ false, /*incOwnRadius*/ false,
                                                /*incTargetRadius*/ false);
        }

        WorldObject const* _source;
        float _range;
    };
}

CoaHazardsValue::CoaHazardsValue(PlayerbotAI* botAI)
    : NearestUnitsValue(botAI, CoaHazardKey::Hazards, HazardRange(), /*ignoreLos*/ true, 500)
{
}

void CoaHazardsValue::FindUnits(std::list<Unit*>& targets)
{
    if (!CoaHazardRegistry::HasEmitters(bot->GetMapId()))
        return;

    RegisteredHazardInRangeCheck check(bot, range);
    Acore::UnitListSearcher<RegisteredHazardInRangeCheck> searcher(bot, targets, check);
    Cell::VisitObjects(bot, searcher, range);
}

bool CoaHazardsValue::AcceptUnit(Unit* unit)
{
    return unit && unit->IsCreature() && CoaHazardRegistry::Find(unit->GetMapId(), unit->GetEntry()) != nullptr;
}

CoaGroundHazardsValue::CoaGroundHazardsValue(PlayerbotAI* botAI)
    : ObjectGuidListCalculatedValue(botAI, CoaHazardKey::GroundHazards, 500), range(HazardRange())
{
}

GuidVector CoaGroundHazardsValue::Calculate()
{
    GuidVector results;
    if (!CoaGroupTank::GuardScope(bot) || !CoaHazardRegistry::HasGroundHazards(bot->GetMapId()))
        return results;

    std::list<WorldObject*> found;
    AnyDynamicObjectInRangeCheck check(bot, range);
    Acore::WorldObjectListSearcher<AnyDynamicObjectInRangeCheck> searcher(bot, found, check,
                                                                          GRID_MAP_TYPE_MASK_DYNAMICOBJECT);
    Cell::VisitObjects(bot, searcher, range);

    for (WorldObject* obj : found)
    {
        if (!obj || !obj->IsDynamicObject())
            continue;

        DynamicObject* dynObj = obj->ToDynObject();
        if (!dynObj || !CoaHazardRegistry::FindGround(bot->GetMapId(), dynObj->GetSpellId()))
            continue;

        results.push_back(dynObj->GetGUID());
    }

    return results;
}

CoaTrapHazardsValue::CoaTrapHazardsValue(PlayerbotAI* botAI)
    : ObjectGuidListCalculatedValue(botAI, CoaHazardKey::TrapHazards, 500), range(HazardRange())
{
}

GuidVector CoaTrapHazardsValue::Calculate()
{
    GuidVector results;
    if (!CoaGroupTank::GuardScope(bot))
        return results;

    std::vector<uint32> const entries = CoaHazardRegistry::TrapEntries(bot->GetMapId());
    if (entries.empty())
        return results;

    std::list<GameObject*> found;
    bot->GetGameObjectListWithEntryInGrid(found, entries, range);

    for (GameObject* go : found)
    {
        if (!go || !go->IsInWorld() || !CoaHazardRegistry::FindTrap(go->GetMapId(), go->GetEntry()))
            continue;
        results.push_back(go->GetGUID());
    }

    return results;
}
