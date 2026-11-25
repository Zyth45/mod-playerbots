/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaStarterWeapon.h"

#include "Bag.h"
#include "CoaFishingGear.h"
#include "CoaMissionMgr.h"
#include "CoaStarterWeaponRules.h"
#include "CoaWorldItemOrigin.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include <mutex>
#include <unordered_set>

namespace
{
std::mutex givenLock;
std::unordered_set<ObjectGuid::LowType> given;  // bots that got their starting weapon once

bool IsWeapon(Item* item)
{
    ItemTemplate const* proto = item ? item->GetTemplate() : nullptr;
    return proto && proto->Class == ITEM_CLASS_WEAPON && !CoaFishingGear::IsFishingPole(item);
}

// A weapon worn (main hand, off hand, ranged) or in the bags; a fishing pole is no weapon.
bool HasWeapon(Player* bot)
{
    for (uint8 slot : {EQUIPMENT_SLOT_MAINHAND, EQUIPMENT_SLOT_OFFHAND, EQUIPMENT_SLOT_RANGED})
        if (IsWeapon(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot)))
            return true;
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (IsWeapon(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot)))
            return true;
    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
        if (Bag* bag = bot->GetBagByPos(bagSlot))
            for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                if (IsWeapon(bag->GetItemByPos(uint8(slot))))
                    return true;
    return false;
}

bool GivenBefore(ObjectGuid::LowType bot)
{
    std::lock_guard<std::mutex> guard(givenLock);
    return given.count(bot) != 0;
}
}  // namespace

void CoaStarterWeapon::Load()
{
    std::lock_guard<std::mutex> guard(givenLock);
    given.clear();
    if (QueryResult result = PlayerbotsDatabase.Query("SELECT `bot` FROM `playerbots_coa_starter_weapon`"))
    {
        do
            given.insert(result->Fetch()[0].Get<uint32>());
        while (result->NextRow());
    }
}

bool CoaStarterWeapon::Ensure(Player* bot)
{
    // The cheap checks first: most realms are not self-found, most bots hold a weapon.
    if (!bot || !sPlayerbotAIConfig.coaSelfFound || !sPlayerbotAIConfig.coaSelfFoundStarterWeapon || !bot->IsAlive() ||
        !bot->IsInWorld())
        return false;
    CoaStarterWeaponRules::View view;
    view.enabled = true;
    view.selfFoundRealm = true;
    view.randomBot = sRandomPlayerbotMgr.IsRandomBot(bot);
    view.hasWeapon = view.randomBot && HasWeapon(bot);
    view.givenBefore = view.randomBot && !view.hasWeapon && GivenBefore(bot->GetGUID().GetCounter());
    if (!CoaStarterWeaponRules::Gives(view))
        return false;

    uint32 const itemId = PlayerbotFactory::StarterWeaponOf(bot);
    if (!itemId)
        return false;
    ItemPosCountVec dest;
    if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemId, 1) != EQUIP_ERR_OK)
        return false;  // full bags: tried again later, it still counts as not given
    Item* item = bot->StoreNewItem(dest, itemId, true);
    if (!item)
        return false;
    {
        std::lock_guard<std::mutex> guard(givenLock);
        given.insert(bot->GetGUID().GetCounter());
    }
    PlayerbotsDatabase.Execute("REPLACE INTO `playerbots_coa_starter_weapon` (`bot`, `item`) VALUES ({}, {})",
                               bot->GetGUID().GetCounter(), itemId);
    // In hand at once, as a player would (the pole, if any, goes to the bags).
    if (bot->CanUseItem(item) == EQUIP_ERR_OK)
    {
        CoaWorld::StartGearScope const startGear;  // CoA World 3.3: "start" in coa_item_origin
        CoaMissionMgr::EquipItem(bot, item, EQUIPMENT_SLOT_MAINHAND);
    }
    return true;
}
