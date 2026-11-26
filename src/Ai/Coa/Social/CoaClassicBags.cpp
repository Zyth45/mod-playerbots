/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaClassicBags.h"

#include "Bag.h"
#include "CoaGearExpansionRules.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "Timer.h"
#include "World.h"
#include <unordered_map>

namespace
{
constexpr uint32 BAG_CHECK_MS = 5 * MINUTE * IN_MILLISECONDS;
}

void CoaClassicBags::Tick(PlayerbotAI* botAI)
{
    Player* bot = botAI ? botAI->GetBot() : nullptr;
    if (!bot)
        return;
    uint32 const target = CoaGearExpansionRules::FactoryBag(sPlayerbotAIConfig.limitGearExpansion,
                                                            sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL));
    if (target == CoaGearExpansionRules::PORTABLE_HOLE)
        return;
    // At the first update after a login, then every five minutes: a few reads of the bag slots.
    thread_local std::unordered_map<ObjectGuid::LowType, uint32> nextCheck;
    uint32 const now = getMSTime();
    uint32& next = nextCheck[bot->GetGUID().GetCounter()];
    if (next && int32(next - now) > 0)
        return;
    if (!bot->IsInWorld() || !bot->IsAlive() || bot->IsInCombat() || bot->IsBeingTeleported())
        return;  // looked at again at the next update
    next = now + BAG_CHECK_MS;
    if (nextCheck.size() > 20000)
        nextCheck.clear();
    if (!sRandomPlayerbotMgr.IsRandomBot(bot))
        return;
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(target);
    if (!proto)
        return;

    for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
    {
        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        Bag* bag = item ? item->ToBag() : nullptr;
        if (!bag || CoaGearExpansionRules::ForSlot(bag->GetEntry(), bag->GetTemplate()->ContainerSlots, bag->IsEmpty(),
                                                   target, proto->ContainerSlots, false, true) !=
                        CoaGearExpansionRules::BagSlot::Replace)
            continue;
        uint16 dest;
        if (bot->CanEquipNewItem(slot, dest, target, true) != EQUIP_ERR_OK)
            continue;
        bot->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);
        bot->EquipNewItem(dest, target, true);
    }
}
