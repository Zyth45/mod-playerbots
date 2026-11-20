/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaFishingGear.h"

#include "Bag.h"
#include "CoaLive.h"
#include "CoaMissionMgr.h"
#include "CoaStarterWeapon.h"
#include "Item.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "Timer.h"
#include "WorldSession.h"
#include <mutex>
#include <unordered_map>

namespace
{
constexpr uint32 CHECK_MS = 2000;

struct Worn
{
    ObjectGuid mainHand;
    ObjectGuid offHand;
};

std::mutex gearLock;
std::unordered_map<ObjectGuid::LowType, Worn> worn;  // bot -> its weapons before the pole

Item* MainHand(Player* bot) { return bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND); }

bool FitsMainHand(ItemTemplate const* proto)
{
    return proto->InventoryType == INVTYPE_WEAPON || proto->InventoryType == INVTYPE_WEAPONMAINHAND ||
           proto->InventoryType == INVTYPE_2HWEAPON;
}

// The best weapon of the bags for the main hand (the highest item level the bot can use), never a pole.
Item* BestWeapon(Player* bot)
{
    Item* best = nullptr;
    auto consider = [&](Item* item)
    {
        ItemTemplate const* proto = item ? item->GetTemplate() : nullptr;
        if (!proto || proto->Class != ITEM_CLASS_WEAPON || CoaFishingGear::IsFishingPole(item) || !FitsMainHand(proto) ||
            bot->CanUseItem(item) != EQUIP_ERR_OK)
            return;
        if (!best || proto->ItemLevel > best->GetTemplate()->ItemLevel)
            best = item;
    };
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        consider(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
        if (Bag* bag = bot->GetBagByPos(bagSlot))
            for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                consider(bag->GetItemByPos(uint8(slot)));
    return best;
}

// In its bags, not worn: an item kept in mind may have been sold, given or destroyed since.
Item* InBags(Player* bot, ObjectGuid guid)
{
    Item* item = guid ? bot->GetItemByGuid(guid) : nullptr;
    return item && !item->IsEquipped() ? item : nullptr;
}
}  // namespace

bool CoaFishingGear::IsFishingPole(Item* item)
{
    ItemTemplate const* proto = item ? item->GetTemplate() : nullptr;
    return proto && proto->Class == ITEM_CLASS_WEAPON && proto->SubClass == ITEM_SUBCLASS_WEAPON_FISHING_POLE;
}

void CoaFishingGear::BeforePole(Player* bot)
{
    if (!bot || IsFishingPole(MainHand(bot)))
        return;
    Item* mainHand = MainHand(bot);
    Item* offHand = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
    std::lock_guard<std::mutex> guard(gearLock);
    if (worn.size() > 20000)
        worn.clear();
    worn[bot->GetGUID().GetCounter()] = {mainHand ? mainHand->GetGUID() : ObjectGuid::Empty,
                                         offHand ? offHand->GetGUID() : ObjectGuid::Empty};
}

bool CoaFishingGear::Restore(Player* bot)
{
    if (!bot || !bot->IsAlive() || !bot->IsInWorld() || !IsFishingPole(MainHand(bot)))
        return false;
    Worn before;
    {
        std::lock_guard<std::mutex> guard(gearLock);
        auto const it = worn.find(bot->GetGUID().GetCounter());
        if (it != worn.end())
        {
            before = it->second;
            worn.erase(it);
        }
    }
    Item* weapon = InBags(bot, before.mainHand);
    if (!weapon)
        weapon = BestWeapon(bot);
    if (!weapon)
        // Nothing to hold but the pole: nothing is created, except the starting weapon of a self-found bot, once.
        return CoaStarterWeapon::Ensure(bot);
    CoaMissionMgr::EquipItem(bot, weapon, EQUIPMENT_SLOT_MAINHAND);
    if (IsFishingPole(MainHand(bot)))
        return false;
    if (Item* offHand = InBags(bot, before.offHand))
        CoaMissionMgr::EquipItem(bot, offHand, EQUIPMENT_SLOT_OFFHAND);
    return true;
}

void CoaFishingGear::Tick(PlayerbotAI* botAI)
{
    Player* bot = botAI ? botAI->GetBot() : nullptr;
    if (!bot || !IsFishingPole(MainHand(bot)))
        return;
    // Every 2 s at most: a bot with no weapon in its bags looks again later, not on every update.
    thread_local std::unordered_map<ObjectGuid::LowType, uint32> nextCheck;
    uint32 const now = getMSTime();
    uint32& next = nextCheck[bot->GetGUID().GetCounter()];
    if (next && int32(next - now) > 0)
        return;
    next = now + CHECK_MS;
    if (nextCheck.size() > 20000)
        nextCheck.clear();

    // Fishing now (its master's "fish", the shore or the city, a fishing mission): the pole stays, out of combat.
    // Any other time (a fight, a group, a recruit, after a login or a refresh) the weapon comes back.
    if (!bot->IsInCombat() && (botAI->HasStrategy("master fishing", BOT_STATE_NON_COMBAT) ||
                               sCoaMissionMgr.IsOnMission(bot) || CoaLive::IsFishing(bot)))
        return;
    Restore(bot);
}
