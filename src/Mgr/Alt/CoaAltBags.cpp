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
std::vector<BagItem> Candidates(PlayerbotAI* botAI, uint32 maxQuality, bool forSale)
{
    Player* bot = botAI->GetBot();
    std::set<uint32>& kept = botAI->GetAiObjectContext()->GetValue<std::set<uint32>&>("always loot list")->Get();
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

    std::vector<BagItem> out;
    for (BagItem const& entry : BagItems(bot))
    {
        ItemTemplate const* proto = entry.item->GetTemplate();
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
        if (!CoaAltBags::Eligible(c, maxQuality, forSale))
            continue;
        if (proto->Class == ITEM_CLASS_WEAPON || proto->Class == ITEM_CLASS_ARMOR)
        {
            ItemUsage const usage = UsageOf(botAI, proto->ItemId, entry.item->GetItemRandomPropertyId());
            if (usage == ITEM_USAGE_EQUIP || usage == ITEM_USAGE_REPLACE)
                continue;
        }
        out.push_back(entry);
    }
    return out;
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
bool Command(PlayerbotAI* botAI, std::string const& command, std::string const& arg)
{
    bool const compare = command == "cmp";
    if (!compare && command != "bags" && command != "sell" && command != "destroy")
        return false;
    if (!botAI || !botAI->GetBot())
        return true;

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
    if (command != "cmp" && command != "bags" && command != "sell" && command != "destroy")
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
