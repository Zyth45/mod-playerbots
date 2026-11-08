/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ZGHelpers.h"
#include "Player.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "CellImpl.h"
#include "DynamicObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "PlayerbotAIConfig.h"

namespace ZulGurubHelpers
{
bool IsGazedByMandokir(Player* bot)
{
    uint32 const gaze = static_cast<uint32>(ZulGurubSpells::SPELL_THREATENING_GAZE);
    if (bot->HasAura(gaze))
        return true;
    // CoA: both gazes are triggered auras, with no cast to read.
    if (ZgCoaFixesOn() && (bot->HasAura(SPELL_COA_THREATENING_GAZE) || bot->HasAura(SPELL_COA_MENACING_GAZE)))
        return true;

    for (auto const& [guid, ref] : bot->GetThreatMgr().GetThreatenedByMeList())
    {
        Unit* unit = ref->GetOwner();
        if (!unit || unit->GetEntry() != static_cast<uint32>(ZulGurubNpcs::NPC_BLOODLORD_MANDOKIR))
            continue;

        Spell const* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        return spell && spell->GetSpellInfo()->Id == gaze && spell->m_targets.GetUnitTargetGUID() == bot->GetGUID();
    }

    return false;
}

bool ZgCoaFixesOn()
{
    return sPlayerbotAIConfig.coaCombatZulGurubEnable;
}

bool LacksPoisonousBlood(Player* bot)
{
    return !bot->HasAura(static_cast<uint32>(ZulGurubSpells::SPELL_POISONOUS_BLOOD)) &&
           !bot->HasAura(static_cast<uint32>(ZulGurubSpells::SPELL_POOL_OF_POISONED_BLOOD));
}

DynamicObject* FindPoisonPool(Player* bot, float range)
{
    std::list<WorldObject*> objs;
    Acore::AllWorldObjectsInRange check(bot, range);
    Acore::WorldObjectListSearcher<Acore::AllWorldObjectsInRange> searcher(bot, objs, check,
                                                                          GRID_MAP_TYPE_MASK_DYNAMICOBJECT);
    Cell::VisitObjects(bot, searcher, range);

    DynamicObject* best = nullptr;
    for (WorldObject* obj : objs)
    {
        DynamicObject* pool = obj->ToDynObject();
        if (!pool || !pool->IsInWorld())
            continue;
        uint32 const id = pool->GetSpellId();
        if (id != static_cast<uint32>(ZulGurubSpells::SPELL_POOL_OF_POISONED_BLOOD) && id != 24320)
            continue;
        if (!best || bot->GetExactDist2d(pool) < bot->GetExactDist2d(best))
            best = pool;
    }
    return best;
}
}  // namespace ZulGurubHelpers
