/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_HAZARD_VALUES_H
#define COA_HAZARD_VALUES_H

#include "NearestUnitsValue.h"
#include "Value.h"

class PlayerbotAI;
class Unit;

namespace CoaHazardKey
{
    inline constexpr char const* Hazards = "coa hazards";               // live creature emitters (Unit guids)
    inline constexpr char const* GroundHazards = "coa ground hazards";  // live ground pools (DynamicObject guids)
    inline constexpr char const* TrapHazards = "coa trap hazards";      // live traps (GameObject guids)
}

// Live registered creature emitters within sight distance, LOS ignored, recomputed at most every 500ms.
class CoaHazardsValue : public NearestUnitsValue
{
public:
    CoaHazardsValue(PlayerbotAI* botAI);

protected:
    void FindUnits(std::list<Unit*>& targets) override;
    bool AcceptUnit(Unit* unit) override;
};

// Live registered persistent-area-aura pools within sight distance (500ms). A DynamicObject is not a unit, so the
// creature sweep above cannot see one.
class CoaGroundHazardsValue : public ObjectGuidListCalculatedValue
{
public:
    CoaGroundHazardsValue(PlayerbotAI* botAI);
    GuidVector Calculate() override;

private:
    float range;
};

// Live registered trap gameobjects within sight distance (500ms), swept by entry.
class CoaTrapHazardsValue : public ObjectGuidListCalculatedValue
{
public:
    CoaTrapHazardsValue(PlayerbotAI* botAI);
    GuidVector Calculate() override;

private:
    float range;
};

#endif
