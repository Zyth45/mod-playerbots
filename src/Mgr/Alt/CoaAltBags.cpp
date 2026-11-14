/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaAltBags.h"

#include "Bag.h"
#include "CoaAltAddon.h"
#include "CoaAltBagsRules.h"
#include "ItemPackets.h"
#include "ItemUsageValue.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "StatsWeightCalculator.h"
#include "WorldSession.h"

#include <cstdio>
#include <set>
#include <sstream>
#include <vector>

namespace
{
constexpr uint32 HEARTHSTONE = 6948;

struct BagItem
{
    uint8 bag;
    uint8 slot;
    Item* item;
};

// The bag items in bag order: the backpack (bag 255), then the four bags.
std::vector<BagItem> BagItems(Player* bot)
{
    std::vector<BagItem> out;
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            out.push_back({INVENTORY_SLOT_BAG_0, slot, item});
    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
        if (Bag* bag = bot->GetBagByPos(bagSlot))
            for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                if (Item* item = bag->GetItemByPos(uint8(slot)))
                    out.push_back({bagSlot, uint8(slot), item});
    return out;
}

ItemUsage UsageOf(PlayerbotAI* botAI, uint32 itemId, int32 randomPropertyId)
{
    std::ostringstream qualifier;
    qualifier << itemId;
    if (randomPropertyId)
        qualifier << "," << randomPropertyId;
    return botAI->GetAiObjectContext()->GetValue<ItemUsage>("item usage", qualifier.str())->Get();
}

// The client's item string, so that the addon shows the bot's real piece (enchant, gems, random suffix).
std::string ItemString(Item* item, uint8 level)
{
    int32 const property = item->GetItemRandomPropertyId();
    std::ostringstream out;
    out << "item:" << item->GetEntry() << ":" << item->GetEnchantmentId(PERM_ENCHANTMENT_SLOT) << ":"
        << item->GetEnchantmentId(SOCK_ENCHANTMENT_SLOT) << ":" << item->GetEnchantmentId(SOCK_ENCHANTMENT_SLOT_2)
        << ":" << item->GetEnchantmentId(SOCK_ENCHANTMENT_SLOT_3) << ":0:" << property << ":"
        << (property < 0 ? item->GetItemSuffixFactor() : 0) << ":" << uint32(level);
    return out.str();
}

std::string Score(float value)
{
    char out[32];
    std::snprintf(out, sizeof(out), "%.1f", value);
    return out;
}

void Compare(PlayerbotAI* botAI, std::string const& arg)
{
    Player* bot = botAI->GetBot();
    uint32 itemId = 0;
    int32 property = 0;
    ItemTemplate const* proto = nullptr;
    if (CoaAltBags::ParseItem(arg, itemId, property))
        proto = sObjectMgr->GetItemTemplate(itemId);
    // Every line of the reply ends with the requested item id (protocol 3.0.1): the addon matches each line to
    // its request, and a reply can no longer land on another item's tooltip.
    std::string const tag = "\t" + std::to_string(itemId);
    if (!proto)
    {
        CoaAltAddon::SendToMaster(botAI, "CMP\terr\tnotfound" + tag);
        CoaAltAddon::SendToMaster(botAI, "CMP\t-" + tag);
        return;
    }

    std::vector<uint8_t> const slots = CoaAltBags::SlotsFor(proto->InventoryType);
    bool const gear = (proto->Class == ITEM_CLASS_WEAPON || proto->Class == ITEM_CLASS_ARMOR) && !slots.empty();
    if (!gear)
    {
        CoaAltAddon::SendToMaster(botAI, "CMP\terr\tnotgear" + tag);
        CoaAltAddon::SendToMaster(botAI, "CMP\t-" + tag);
        return;
    }

    Item* held = nullptr;
    for (BagItem const& entry : BagItems(bot))
        if (entry.item->GetEntry() == itemId)
        {
            held = entry.item;
            property = held->GetItemRandomPropertyId();
            break;
        }

    StatsWeightCalculator calculator(bot);
    calculator.SetItemSetBonus(false);
    calculator.SetOverflowPenalty(false);
    if (sRandomPlayerbotMgr.IsSpecPvp(bot->GetGUID().GetCounter(), bot->getClass()))
        calculator.SetPvpSpec(true);

    bool const canUse = bot->CanUseItem(proto) == EQUIP_ERR_OK;
    ItemUsage const usage = UsageOf(botAI, itemId, property);

    std::ostringstream line;
    line << "CMP\titem\t" << itemId << "\t" << (held ? 1 : 0) << "\t" << proto->InventoryType << "\t"
         << CoaAltBags::Verdict(true, canUse, usage) << "\t"
         << Score(canUse ? calculator.CalculateItem(itemId, property) : 0.0f) << "\t";
    if (held)
        line << ItemString(held, bot->GetLevel());
    else
        line << "item:" << itemId << ":0:0:0:0:0:" << property << ":0:" << uint32(bot->GetLevel());
    CoaAltAddon::SendToMaster(botAI, line.str());

    for (uint8_t slot : slots)
    {
        std::ostringstream worn;
        worn << "CMP\tworn\t" << uint32(slot) + 1 << "\t";
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            worn << item->GetEntry() << "\t"
                 << Score(calculator.CalculateItem(item->GetEntry(), item->GetItemRandomPropertyId())) << "\t"
                 << ItemString(item, bot->GetLevel());
        else
            worn << "0\t0\t-";
        worn << tag;
        CoaAltAddon::SendToMaster(botAI, worn.str());
    }
    CoaAltAddon::SendToMaster(botAI, "CMP\t-" + tag);
}

// The bag items that may go, with the exclusions of the protocol.
// What the rules need to know of one bag item (CoaAltBags::Candidate).
CoaAltBags::Candidate Describe(PlayerbotAI* botAI, Item* item, std::set<uint32> const& kept, std::set<uint32> const& outfit)
{
    ItemTemplate const* proto = item->GetTemplate();
    CoaAltBags::Candidate c;
    c.quality = proto->Quality;
    c.quest = proto->Class == ITEM_CLASS_QUEST || proto->StartQuest || proto->Bonding == BIND_QUEST_ITEM;
    for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
        if (proto->Spells[i].SpellId > 0 && proto->Spells[i].SpellTrigger == ITEM_SPELLTRIGGER_ON_USE)
            c.useSpell = true;
    c.useSpell = c.useSpell || proto->ItemId == HEARTHSTONE || proto->HasFlag(ITEM_FLAG_HAS_LOOT);
    c.kept = kept.count(proto->ItemId) != 0;
    c.outfit = outfit.count(proto->ItemId) != 0;
    c.container = proto->Class == ITEM_CLASS_CONTAINER || proto->Class == ITEM_CLASS_QUIVER;
    c.ammo = proto->Class == ITEM_CLASS_PROJECTILE;
    c.tool = CoaAltBags::IsProfessionItem(proto->Class, proto->SubClass, proto->TotemCategory);
    c.conjured = proto->HasFlag(ITEM_FLAG_CONJURED);
    c.noPrice = proto->SellPrice == 0;
    if (proto->Class == ITEM_CLASS_WEAPON || proto->Class == ITEM_CLASS_ARMOR)
    {
        ItemUsage const usage = UsageOf(botAI, proto->ItemId, item->GetItemRandomPropertyId());
        c.upgrade = usage == ITEM_USAGE_EQUIP || usage == ITEM_USAGE_REPLACE;
    }
    return c;
}

// The items "ll +[item]" keeps, and those of the saved outfits.
std::set<uint32> OutfitItems(PlayerbotAI* botAI)
{
    std::set<uint32> outfit;
    for (std::string const& line :
         botAI->GetAiObjectContext()->GetValue<std::vector<std::string>&>("outfit list")->Get())
    {
        size_t pos = line.find('=');
        while (pos != std::string::npos && pos + 1 < line.size())
        {
            outfit.insert(uint32(std::strtoul(line.c_str() + pos + 1, nullptr, 10)));
            pos = line.find(',', pos + 1);
        }
    }
    return outfit;
}

std::vector<BagItem> Candidates(PlayerbotAI* botAI, uint32 maxQuality, bool forSale)
{
    Player* bot = botAI->GetBot();
    std::set<uint32>& kept = botAI->GetAiObjectContext()->GetValue<std::set<uint32>&>("always loot list")->Get();
    std::set<uint32> const outfit = OutfitItems(botAI);

    std::vector<BagItem> out;
    for (BagItem const& entry : BagItems(bot))
        if (CoaAltBags::Eligible(Describe(botAI, entry.item, kept, outfit), maxQuality, forSale))
            out.push_back(entry);
    return out;
}

// "alt bagcheck <sell|destroy> <id> ...": for each item the player picked, whether it may go, and why not.
void BagCheck(PlayerbotAI* botAI, std::string const& arg)
{
    std::istringstream in(arg);
    std::string verb;
    in >> verb;
    bool const forSale = verb == "sell";
    if (!forSale && verb != "destroy")
        verb = "destroy";
    std::set<uint32>& kept = botAI->GetAiObjectContext()->GetValue<std::set<uint32>&>("always loot list")->Get();
    std::set<uint32> const outfit = OutfitItems(botAI);
    std::vector<BagItem> const items = BagItems(botAI->GetBot());
    std::string token;
    size_t done = 0;
    while (in >> token && done < CoaAltBags::BAGCHECK_MAX)
    {
        uint32 const id = uint32(std::strtoul(token.c_str(), nullptr, 10));
        if (!id)
            continue;
        ++done;
        char const* reason = "notfound";
        for (BagItem const& entry : items)
            if (entry.item->GetEntry() == id)
            {
                reason = CoaAltBags::Refusal(Describe(botAI, entry.item, kept, outfit), forSale);
                break;
            }
        CoaAltAddon::SendToMaster(botAI, "BAGCHK\tit\t" + std::to_string(id) + "\t" + reason);
    }
    CoaAltAddon::SendToMaster(botAI, "BAGCHK\t-\t" + verb);
}

std::vector<CoaAltBags::Entry> Entries(std::vector<BagItem> const& items)
{
    std::vector<CoaAltBags::Entry> out;
    for (BagItem const& entry : items)
        out.push_back({entry.bag, entry.slot, entry.item->GetEntry(), entry.item->GetCount()});
    return out;
}

void Error(PlayerbotAI* botAI, char const* reason)
{
    CoaAltAddon::SendToMaster(botAI, std::string("BAGS\terr\t") + reason);
    CoaAltAddon::SendToMaster(botAI, "BAGS\t-");
}

Creature* VendorInReach(PlayerbotAI* botAI)
{
    for (ObjectGuid const guid : botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest npcs")->Get())
        if (Creature* vendor = botAI->GetBot()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_VENDOR))
            return vendor;
    return nullptr;
}

void Bags(PlayerbotAI* botAI, std::string const& verb, std::string const& arg)
{
    Player* bot = botAI->GetBot();
    std::istringstream in(arg);
    long quality = -1;
    std::string token;
    std::string skipWord;
    std::string skipList;
    in >> quality >> token >> skipWord >> skipList;
    if (quality < 0 || quality > long(CoaAltBags::MAX_QUALITY))
        return Error(botAI, "quality");
    if (!bot->IsAlive())
        return Error(botAI, "dead");
    if (verb != "bags" && bot->IsInCombat())
        return Error(botAI, "combat");

    bool const sell = verb == "sell";
    // "bags <q>" previews a sale, "bags <q> destroy" a destruction (worthless and conjured items in it too).
    bool const forSale = verb == "sell" || (verb == "bags" && token != "destroy");
    std::vector<BagItem> const items = Candidates(botAI, uint32(quality), forSale);

    if (verb == "bags")
    {
        uint64 total = 0;
        for (BagItem const& entry : items)
        {
            ItemTemplate const* proto = entry.item->GetTemplate();
            uint64 const price = uint64(proto->SellPrice) * entry.item->GetCount();
            total += price;
            std::ostringstream line;
            line << "BAGS\tit\t" << uint32(entry.bag) << "\t" << uint32(entry.slot) << "\t" << proto->ItemId << "\t"
                 << entry.item->GetCount() << "\t" << proto->Quality << "\t" << price << "\t"
                 << CoaAltAddon::Field(proto->Name1, 40);
            CoaAltAddon::SendToMaster(botAI, line.str());
        }
        std::ostringstream sum;
        sum << "BAGS\tsum\t" << items.size() << "\t" << total << "\t" << CoaAltBags::Token(Entries(items));
        CoaAltAddon::SendToMaster(botAI, sum.str());
        CoaAltAddon::SendToMaster(botAI, "BAGS\t-");
        return;
    }

    if (token.empty() || token != CoaAltBags::Token(Entries(items)))
        return Error(botAI, "changed");
    std::set<std::pair<uint8_t, uint8_t>> const skip =
        skipWord == "skip" ? CoaAltBags::ParseSkip(skipList) : std::set<std::pair<uint8_t, uint8_t>>();

    Creature* vendor = sell ? VendorInReach(botAI) : nullptr;
    if (sell && !vendor)
        return Error(botAI, "novendor");

    uint32 done = 0;
    uint32 const moneyBefore = bot->GetMoney();
    for (BagItem const& entry : items)
    {
        if (skip.count({entry.bag, entry.slot}))
            continue;
        // Looked up again by position: the item there must still be the one of the preview.
        Item* item = bot->GetItemByPos(entry.bag, entry.slot);
        if (!item || item != entry.item)
            continue;
        if (sell)
        {
            WorldPacket packet(CMSG_SELL_ITEM);
            packet << vendor->GetGUID() << item->GetGUID() << uint32(item->GetCount());
            WorldPackets::Item::SellItem sellPacket(std::move(packet));
            sellPacket.Read();
            bot->GetSession()->HandleSellItemOpcode(sellPacket);
        }
        else
            bot->DestroyItem(entry.bag, entry.slot, true);
        ++done;
    }

    uint32 const earned = bot->GetMoney() > moneyBefore ? bot->GetMoney() - moneyBefore : 0;
    std::ostringstream line;
    line << "BAGS\tdone\t" << verb << "\t" << done << "\t" << earned;
    CoaAltAddon::SendToMaster(botAI, line.str());
    CoaAltAddon::SendToMaster(botAI, "BAGS\t-");
}
}  // namespace

namespace CoaAltBags
{
std::array<uint32_t, 4> BagSizes(Player* bot)
{
    std::array<uint32_t, 4> sizes{};
    for (uint8 i = 0; i < 4; ++i)
        if (Bag* bag = bot->GetBagByPos(INVENTORY_SLOT_BAG_START + i))
            sizes[i] = bag->GetBagSize();
    return sizes;
}

bool PutOnBag(Player* bot, Item* bag, uint8_t slot, ItemTemplate const** replaced)
{
    uint8 const bagSlot = INVENTORY_SLOT_BAG_START + slot;
    if (replaced)
        *replaced = nullptr;
    if (Bag* old = bot->GetBagByPos(bagSlot))
    {
        if (old == bag)
            return true;
        if (replaced)
            *replaced = old->GetTemplate();
        // The items of the old bag: to free slots of the backpack and the other bags (never into the old bag).
        std::vector<Item*> items;
        for (uint32 i = 0; i < old->GetBagSize(); ++i)
            if (Item* item = old->GetItemByPos(uint8(i)))
                items.push_back(item);
        std::vector<std::pair<uint8, uint8>> free;
        for (uint8 s = INVENTORY_SLOT_ITEM_START; s < INVENTORY_SLOT_ITEM_END; ++s)
            if (!bot->GetItemByPos(INVENTORY_SLOT_BAG_0, s))
                free.push_back({INVENTORY_SLOT_BAG_0, s});
        for (uint8 b = INVENTORY_SLOT_BAG_START; b < INVENTORY_SLOT_BAG_END; ++b)
            if (b != bagSlot)
                if (Bag* other = bot->GetBagByPos(b))
                    for (uint32 i = 0; i < other->GetBagSize(); ++i)
                        if (!other->GetItemByPos(uint8(i)))
                            free.push_back({b, uint8(i)});
        std::vector<size_t> const to = AssignSlots(items.size(), free.size(), [&](size_t i, size_t s)
        {
            ItemPosCountVec dest;
            return bot->CanStoreItem(free[s].first, free[s].second, dest, items[i], false) == EQUIP_ERR_OK;
        });
        if (to.size() != items.size())
            return false;
        for (size_t i = 0; i < items.size(); ++i)
            bot->SwapItem(items[i]->GetPos(), uint16(free[to[i]].first) << 8 | free[to[i]].second);
        if (!old->IsEmpty())
            return false;
    }
    bot->SwapItem(bag->GetPos(), uint16(INVENTORY_SLOT_BAG_0) << 8 | bagSlot);
    return bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bagSlot) == bag;
}

namespace
{
// "alt bagslots": the four bag slots.
void BagSlots(PlayerbotAI* botAI)
{
    Player* bot = botAI->GetBot();
    for (uint8 i = 0; i < 4; ++i)
    {
        std::ostringstream line;
        line << "BAGSLOT\tslot\t" << uint32(i) + 1 << "\t";
        if (Bag* bag = bot->GetBagByPos(INVENTORY_SLOT_BAG_START + i))
            line << bag->GetEntry() << "\t" << bag->GetBagSize() << "\t" << bag->GetFreeSlots() << "\t"
                 << ItemString(bag, bot->GetLevel());
        else
            line << "0\t0\t0\t-";
        line << "\tbagslots";
        CoaAltAddon::SendToMaster(botAI, line.str());
    }
    CoaAltAddon::SendToMaster(botAI, "BAGSLOT\t-\tbagslots");
}

// "alt bagequip <itemId|itemString> [1..4]": a bag of the bot's bags put in a bag slot.
void BagEquip(PlayerbotAI* botAI, std::string const& arg)
{
    Player* bot = botAI->GetBot();
    uint32 itemId = 0;
    int32 property = 0;
    CoaAltBags::ParseItem(arg, itemId, property);
    std::string const tag = "\t" + std::to_string(itemId);
    auto const fail = [&](char const* reason)
    {
        CoaAltAddon::SendToMaster(botAI, std::string("BAGSLOT\terr\t") + reason + tag);
        CoaAltAddon::SendToMaster(botAI, "BAGSLOT\t-" + tag);
    };
    // The slot asked for: a last word 1..4 after the item.
    uint32 requested = 0;
    size_t const last = arg.find_last_of(' ');
    if (last != std::string::npos && arg.size() == last + 2 && arg[last + 1] >= '1' && arg[last + 1] <= '4')
        requested = uint32(arg[last + 1] - '0');
    if (!bot->IsAlive())
        return fail("dead");
    if (bot->IsInCombat())
        return fail("combat");
    Item* bag = nullptr;
    for (BagItem const& entry : BagItems(bot))
        if (entry.item->GetEntry() == itemId)
        {
            bag = entry.item;
            break;
        }
    if (!bag)
        return fail("notfound");
    ItemTemplate const* proto = bag->GetTemplate();
    if (proto->Class != ITEM_CLASS_CONTAINER && proto->Class != ITEM_CLASS_QUIVER)
        return fail("notbag");
    int const slot = CoaAltBags::PickBagSlot(BagSizes(bot), proto->ContainerSlots, requested);
    if (slot < 0 || !PutOnBag(bot, bag, uint8(slot), nullptr))
        return fail("nospace");
    CoaAltAddon::SendToMaster(botAI, "BAGSLOT\tdone\t" + std::to_string(slot + 1) + "\t" + std::to_string(itemId) + tag);
    CoaAltAddon::SendToMaster(botAI, "BAGSLOT\t-" + tag);
}
}  // namespace

bool Command(PlayerbotAI* botAI, std::string const& command, std::string const& arg)
{
    bool const compare = command == "cmp";
    bool const slots = command == "bagslots" || command == "bagequip";
    bool const check = command == "bagcheck";
    if (!compare && !slots && !check && command != "bags" && command != "sell" && command != "destroy")
        return false;
    if (!botAI || !botAI->GetBot())
        return true;

    if (check)
    {
        if (!sPlayerbotAIConfig.coaAltBags)
        {
            CoaAltAddon::SendToMaster(botAI, "BAGCHK\terr\toff");
            CoaAltAddon::SendToMaster(botAI, "BAGCHK\t-");
        }
        else
            BagCheck(botAI, arg);
        return true;
    }

    if (slots)
    {
        if (!sPlayerbotAIConfig.coaAltBags)
        {
            std::string const tag = command == "bagslots" ? "\tbagslots" : "\t0";
            CoaAltAddon::SendToMaster(botAI, "BAGSLOT\terr\toff" + tag);
            CoaAltAddon::SendToMaster(botAI, "BAGSLOT\t-" + tag);
        }
        else if (command == "bagslots")
            BagSlots(botAI);
        else
            BagEquip(botAI, arg);
        return true;
    }

    if (!sPlayerbotAIConfig.coaAltBags)
    {
        CoaAltAddon::SendToMaster(botAI, compare ? "CMP\terr\toff" : "BAGS\terr\toff");
        CoaAltAddon::SendToMaster(botAI, compare ? "CMP\t-" : "BAGS\t-");
        return true;
    }

    if (compare)
        Compare(botAI, arg);
    else
        Bags(botAI, command, arg);
    return true;
}

bool Enqueue(PlayerbotAI* botAI, std::string const& request)
{
    std::string const command = request.substr(0, request.find(' '));
    if (command != "cmp" && command != "bags" && command != "sell" && command != "destroy" && command != "bagslots" &&
        command != "bagequip" && command != "bagcheck")
        return false;
    if (!botAI)
        return true;
    std::lock_guard<std::mutex> guard(botAI->coaAltBagsLock);
    if (botAI->coaAltBagsQueue.size() < CoaAltBags::QUEUE_MAX)
        botAI->coaAltBagsQueue.push_back(request);
    else if (command == "cmp")
    {
        // Too many at once: answered at once, busy, so the addon stops waiting on it.
        uint32_t itemId = 0;
        int32_t property = 0;
        CoaAltBags::ParseItem(request.size() > 4 ? request.substr(4) : std::string(), itemId, property);
        CoaAltAddon::SendToMaster(botAI, "CMP\terr\tbusy\t" + std::to_string(itemId));
        CoaAltAddon::SendToMaster(botAI, "CMP\t-\t" + std::to_string(itemId));
    }
    return true;
}

void Drain(PlayerbotAI* botAI)
{
    std::vector<std::string> requests;
    {
        std::lock_guard<std::mutex> guard(botAI->coaAltBagsLock);
        requests.swap(botAI->coaAltBagsQueue);
    }
    for (std::string const& request : requests)
    {
        size_t const space = request.find(' ');
        std::string const command = request.substr(0, space);
        std::string arg = space == std::string::npos ? std::string() : request.substr(space + 1);
        arg.erase(0, arg.find_first_not_of(' '));
        Command(botAI, command, arg);
    }
}
}  // namespace CoaAltBags
