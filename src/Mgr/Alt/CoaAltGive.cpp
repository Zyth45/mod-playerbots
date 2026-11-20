/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaAltGive.h"

#include "Bag.h"
#include "CoaAltAddon.h"
#include "CoaAltGiveRules.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "Item.h"
#include "Mail.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "Timer.h"
#include <atomic>
#include <mutex>
#include <sstream>
#include <vector>

namespace
{
constexpr uint32 UNCLAIMED_MS = 10 * IN_MILLISECONDS;

// Items on their way from an alt to another: out of the giver's bags, not yet in the receiver's.
struct Parcel
{
    ObjectGuid from;
    ObjectGuid to;
    std::vector<Item*> items;  // whole stacks, moved as they are
    uint32 itemId = 0;
    uint32 fresh = 0;          // the part of a split stack, created again on arrival
    uint32 at = 0;             // getMSTime() of the hand-off
};

std::mutex parcelLock;
std::vector<Parcel> parcels;
std::atomic<uint32> parcelCount{0};

void Answer(PlayerbotAI* botAI, std::string const& payload) { CoaAltAddon::SendToMaster(botAI, payload); }

void Error(PlayerbotAI* botAI, uint32 itemId, char const* reason)
{
    Answer(botAI, "GIVE\terr\t" + std::to_string(itemId) + "\t" + reason);
}

bool Bound(Item const* item) { return item->IsSoulBound() && !item->IsBoundAccountWide(); }

// The items of that id in the bot's bags (the backpack and the four bags), not equipped, not in a trade.
std::vector<Item*> ItemsOf(Player* bot, uint32 itemId)
{
    std::vector<Item*> found;
    auto consider = [&](Item* item)
    {
        if (item && item->GetEntry() == itemId && !item->IsInTrade() && !item->IsNotEmptyBag())
            found.push_back(item);
    };
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        consider(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
        if (Bag* bag = bot->GetBagByPos(bagSlot))
            for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                consider(bag->GetItemByPos(uint8(slot)));
    return found;
}

// An item sent by mail to the receiver, from the giver (the receiver went offline, or its bags filled up).
void Mail(ObjectGuid from, ObjectGuid to, Item* item)
{
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    item->DeleteFromInventoryDB(trans);  // a stack taken whole still has its row in the giver's bags
    item->SetOwnerGUID(to);
    item->SaveToDB(trans);
    MailDraft("Items", "From your alt.")
        .AddItem(item)
        .SendMailTo(trans, MailReceiver(to.GetCounter()), MailSender(MAIL_NORMAL, from.GetCounter()),
                    MAIL_CHECK_MASK_COPIED);
    CharacterDatabase.CommitTransaction(trans);
}

void MailParcel(Parcel& parcel)
{
    for (Item* item : parcel.items)
        Mail(parcel.from, parcel.to, item);
    if (parcel.fresh)
        if (Item* item = Item::CreateItem(parcel.itemId, parcel.fresh, nullptr))
            Mail(parcel.from, parcel.to, item);
}

void Receive(Player* bot, Parcel& parcel)
{
    for (Item* item : parcel.items)
    {
        ItemPosCountVec dest;
        if (bot->CanStoreItem(NULL_BAG, NULL_SLOT, dest, item, false) == EQUIP_ERR_OK)
            bot->MoveItemToInventory(dest, item, true, true);
        else
            Mail(parcel.from, parcel.to, item);
    }
    if (parcel.fresh)
    {
        ItemPosCountVec dest;
        if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, parcel.itemId, parcel.fresh) == EQUIP_ERR_OK)
            bot->StoreNewItem(dest, parcel.itemId, true);
        else if (Item* item = Item::CreateItem(parcel.itemId, parcel.fresh, nullptr))
            Mail(parcel.from, parcel.to, item);
    }
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    bot->SaveInventoryAndGoldToDB(trans);
    CharacterDatabase.CommitTransaction(trans);
}
}  // namespace

bool CoaAltGive::Command(PlayerbotAI* botAI, std::string const& command, std::string const& arg)
{
    if (command != "give")
        return false;
    Player* bot = botAI ? botAI->GetBot() : nullptr;
    Player* master = botAI ? botAI->GetMaster() : nullptr;
    if (!bot || !master)
        return true;

    // The name is the rest of the line: a CoA name may hold a space ("Alt Mining"), read as "Alt" by a single >>.
    CoaAltGiveRules::Give const give = CoaAltGiveRules::Parse(arg);
    uint32 const itemId = give.itemId, count = give.count;
    std::string const& name = give.name;
    if (!sPlayerbotAIConfig.coaGiveToAlt)
        return Error(botAI, itemId, "off"), true;
    if (!itemId || !count || name.empty())
        return Error(botAI, itemId, "missing"), true;

    // Both alts of this master, never a recruited bot.
    Player* receiver = ObjectAccessor::FindPlayerByName(name, true);
    PlayerbotAI* receiverAI = receiver ? GET_PLAYERBOT_AI(receiver) : nullptr;
    if (!botAI->IsAltBot() || !receiver || receiver == bot || !receiverAI || !receiverAI->IsAltBot() ||
        receiverAI->GetMaster() != master)
        return Error(botAI, itemId, "notalt"), true;
    // Both in the master's group: the one condition, wherever they stand.
    Group* group = master->GetGroup();
    if (!group || bot->GetGroup() != group || receiver->GetGroup() != group)
        return Error(botAI, itemId, "notgroup"), true;

    std::vector<Item*> items = ItemsOf(bot, itemId);
    uint32 free = 0, all = 0;
    for (Item* item : items)
    {
        all += item->GetCount();
        if (!Bound(item))
            free += item->GetCount();
    }
    if (free < count)
        return Error(botAI, itemId, all >= count ? "bound" : "missing"), true;

    // Room in the receiver's bags: read from here, checked again on arrival (the mail takes what does not fit).
    ItemPosCountVec room;
    if (receiver->CanStoreNewItem(NULL_BAG, NULL_SLOT, room, itemId, count) != EQUIP_ERR_OK)
        return Error(botAI, itemId, "full"), true;

    Parcel parcel;
    parcel.from = bot->GetGUID();
    parcel.to = receiver->GetGUID();
    parcel.itemId = itemId;
    parcel.at = getMSTime();
    uint32 left = count;
    for (Item* item : items)
    {
        if (!left)
            break;
        if (Bound(item))
            continue;
        uint32 const stack = item->GetCount();
        if (stack <= left)
        {
            item->SetGuidValue(ITEM_FIELD_GIFTCREATOR, bot->GetGUID());
            bot->MoveItemFromInventory(item->GetBagSlot(), item->GetSlot(), true);
            parcel.items.push_back(item);
            left -= stack;
        }
        else
        {
            item->SetCount(stack - left);
            item->SetState(ITEM_CHANGED, bot);
            parcel.fresh = left;
            left = 0;
        }
    }
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    bot->SaveInventoryAndGoldToDB(trans);
    CharacterDatabase.CommitTransaction(trans);
    {
        std::lock_guard<std::mutex> guard(parcelLock);
        parcels.push_back(std::move(parcel));
        parcelCount.store(uint32(parcels.size()), std::memory_order_relaxed);
    }
    Answer(botAI, "GIVE\tdone\t" + std::to_string(itemId) + "\t" + std::to_string(count) + "\t" + receiver->GetName());
    return true;
}

void CoaAltGive::Tick(PlayerbotAI* botAI)
{
    if (!parcelCount.load(std::memory_order_relaxed) || !botAI || !botAI->GetBot())
        return;
    Player* bot = botAI->GetBot();
    ObjectGuid const guid = bot->GetGUID();
    uint32 const now = getMSTime();
    std::vector<Parcel> mine, unclaimed;
    {
        std::lock_guard<std::mutex> guard(parcelLock);
        for (auto it = parcels.begin(); it != parcels.end();)
        {
            if (it->to == guid)
                mine.push_back(std::move(*it));
            else if (it->from == guid && getMSTimeDiff(it->at, now) > UNCLAIMED_MS)
                unclaimed.push_back(std::move(*it));
            else
            {
                ++it;
                continue;
            }
            it = parcels.erase(it);
        }
        parcelCount.store(uint32(parcels.size()), std::memory_order_relaxed);
    }
    if (bot->IsInWorld() && !bot->IsBeingTeleported())
        for (Parcel& parcel : mine)
            Receive(bot, parcel);
    else
        for (Parcel& parcel : mine)
            MailParcel(parcel);
    for (Parcel& parcel : unclaimed)
        MailParcel(parcel);
}
