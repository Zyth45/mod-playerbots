/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "AddLootAction.h"
#include "CoaLootRules.h"
#include "CellImpl.h"
#include "DBCStores.h"
#include "Event.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "LootObjectStack.h"
#include "Playerbots.h"
#include "ServerFacade.h"

namespace
{
bool HerbOrMiningLock(GameObject const* go)
{
    LockEntry const* lockInfo = sLockStore.LookupEntry(go->GetGOInfo()->GetLockId());
    if (!lockInfo)
        return false;

    for (uint8 i = 0; i < MAX_LOCK_CASE; ++i)
    {
        if (lockInfo->Type[i] != LOCK_KEY_SKILL)
            continue;

        SkillType const skill = SkillByLockType(LockType(lockInfo->Index[i]));
        if (skill == SKILL_HERBALISM || skill == SKILL_MINING)
            return true;
    }
    return false;
}

bool OpenWithoutOrder(PlayerbotAI* botAI, Player* bot, ObjectGuid guid)
{
    if (sPlayerbotAIConfig.openChestsWithoutOrder || !guid.IsGameObject())
        return true;

    GameObject* go = botAI->GetGameObject(guid);
    if (!go)
        return true;

    bool const isChest = go->GetGoType() == GAMEOBJECT_TYPE_CHEST;
    return CoaOpenWithoutOrder(false, isChest, isChest && HerbOrMiningLock(go),
                               isChest && LootObject::NeedsQuestItemFrom(bot, go));
}
}

bool AddLootAction::Execute(Event event)
{
    ObjectGuid guid = event.getObject();
    if (!guid)
        return false;

    if (!OpenWithoutOrder(botAI, bot, guid))
        return false;

    return AI_VALUE(LootObjectStack*, "available loot")->Add(guid);
}

bool AddAllLootAction::Execute(Event event)
{
    // "add all loot" told in chat is an order: chests are taken whatever OpenChestsWithoutOrder says.
    ordered = event.getOwner() != nullptr;
    bool added = false;

    GuidVector gos = context->GetValue<GuidVector>("nearest game objects")->Get();
    for (GuidVector::iterator i = gos.begin(); i != gos.end(); i++)
        added |= AddLoot(*i);

    GuidVector corpses = context->GetValue<GuidVector>("nearest corpses")->Get();
    for (GuidVector::iterator i = corpses.begin(); i != corpses.end(); i++)
        added |= AddLoot(*i);

    return added;
}

bool AddLootAction::isUseful() { return true; }

bool AddAllLootAction::isUseful() { return true; }

bool AddAllLootAction::AddLoot(ObjectGuid guid)
{
    if (!ordered && !OpenWithoutOrder(botAI, bot, guid))
        return false;

    return AI_VALUE(LootObjectStack*, "available loot")->Add(guid);
}

bool AddGatheringLootAction::AddLoot(ObjectGuid guid)
{
    LootObject loot(bot, guid);

    WorldObject* wo = loot.GetWorldObject(bot);
    if (loot.IsEmpty() || !wo)
        return false;

    if (loot.skillId == SKILL_NONE)
        return false;

    if (!loot.IsLootPossible(bot))
        return false;

    // No free slot at all, alts included: the node could not be emptied and would be taken again every cycle.
    if (AI_VALUE(uint8, "bag space") >= 100)
        return false;

    return AddAllLootAction::AddLoot(guid);
}
