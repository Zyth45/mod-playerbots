/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaRpgTrade.h"

#include "CoaBotAuctions.h"
#include "CoaMissionMgr.h"
#include "Creature.h"
#include "Event.h"
#include "Item.h"
#include "ItemPackets.h"
#include "ItemUsageValue.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "WorldPacket.h"
#include "WorldSession.h"

namespace CoaEconomy::RpgTrade
{

namespace
{
    // Bags this full: everything sellable goes to the vendor, auction loot included, to make room.
    constexpr uint8 BagsNearlyFull = 95;

    Creature* AsNpc(WorldObject* object, uint32 npcFlag)
    {
        Creature* creature = object ? object->ToCreature() : nullptr;
        return creature && creature->IsInWorld() && creature->IsAlive() && creature->HasNpcFlag(NPCFlags(npcFlag))
            ? creature : nullptr;
    }

    uint8 BagUsed(PlayerbotAI* botAI)
    {
        return botAI->GetAiObjectContext()->GetValue<uint8>("bag space")->Get();
    }

    ItemUsage UsageOf(PlayerbotAI* botAI, Item* item)
    {
        return botAI->GetAiObjectContext()->GetValue<ItemUsage>("item usage", item->GetEntry())->Get();
    }

    // Calls fn for every item in the backpack and the bags.
    template <typename Fn>
    void ForEachBagItem(Player* bot, Fn&& fn)
    {
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                fn(item);
        for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
            if (Bag* bag = bot->GetBagByPos(bagSlot))
                for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                    if (Item* item = bag->GetItemByPos(uint8(slot)))
                        fn(item);
    }

    bool HasAuctionLoot(PlayerbotAI* botAI)
    {
        bool found = false;
        ForEachBagItem(botAI->GetBot(), [&](Item* item)
        {
            if (!found && !item->IsSoulBound() && UsageOf(botAI, item) == ITEM_USAGE_AH)
                found = true;
        });
        return found;
    }

    bool WantsVendor(PlayerbotAI* botAI)
    {
        return sPlayerbotAIConfig.coaEconomyRpgSellEnable && BagUsed(botAI) >= sPlayerbotAIConfig.coaEconomyRpgSellBagPercent &&
               botAI->GetAiObjectContext()->GetValue<bool>("can sell")->Get();
    }

    bool WantsAuctioneer(PlayerbotAI* botAI)
    {
        return BotAuctions::IsEnabled() && !BotAuctions::IsOnCooldown(botAI->GetBot()->GetGUID()) && HasAuctionLoot(botAI);
    }

    // What goes to the vendor: what the bot's item usage marks for a vendor, and its auction loot too when the bot
    // does not use the auction house or its bags are nearly full.
    uint32 SellToVendor(PlayerbotAI* botAI, Creature* vendor)
    {
        Player* bot = botAI->GetBot();
        // A bot on a gathering mission keeps what it gathers for its owner (as SellAction).
        if (sCoaMissionMgr.IsOnMission(bot))
            return 0;
        bool const auctionLootToo = !BotAuctions::IsEnabled() || BagUsed(botAI) >= BagsNearlyFull;
        std::vector<Item*> toSell;
        ForEachBagItem(bot, [&](Item* item)
        {
            ItemTemplate const* proto = item->GetTemplate();
            if (!proto || !proto->SellPrice || item->IsInTrade())
                return;
            ItemUsage const usage = UsageOf(botAI, item);
            if (usage == ITEM_USAGE_VENDOR || (auctionLootToo && usage == ITEM_USAGE_AH))
                toSell.push_back(item);
        });

        for (Item* item : toSell)
        {
            WorldPacket packet(CMSG_SELL_ITEM);
            packet << vendor->GetGUID() << item->GetGUID() << uint32(item->GetCount());
            WorldPackets::Item::SellItem sell(std::move(packet));
            sell.Read();
            bot->GetSession()->HandleSellItemOpcode(sell);
        }
        return uint32(toSell.size());
    }
}

bool WantsTrade(PlayerbotAI* botAI)
{
    if (!botAI || !botAI->GetBot())
        return false;
    return WantsVendor(botAI) || WantsAuctioneer(botAI);
}

ObjectGuid ChooseTradeNpc(PlayerbotAI* botAI, std::vector<ObjectGuid> const& targets, float distanceLimit)
{
    Player* bot = botAI->GetBot();
    bool const vendor = WantsVendor(botAI);
    bool const auctioneer = WantsAuctioneer(botAI);
    if (!vendor && !auctioneer)
        return ObjectGuid();

    Creature* nearest = nullptr;
    for (ObjectGuid const& guid : targets)
    {
        WorldObject* object = ObjectAccessor::GetWorldObject(*bot, guid);
        Creature* npc = (vendor ? AsNpc(object, UNIT_NPC_FLAG_VENDOR) : nullptr);
        if (!npc && auctioneer)
            npc = AsNpc(object, UNIT_NPC_FLAG_AUCTIONEER);
        if (!npc)
            continue;
        if (distanceLimit > 0.0f && bot->GetDistance(npc) > distanceLimit)
            continue;
        if (!nearest || bot->GetExactDist(npc) < bot->GetExactDist(nearest))
            nearest = npc;
    }
    return nearest ? nearest->GetGUID() : ObjectGuid();
}

bool TradeAtNpc(PlayerbotAI* botAI, WorldObject* npc)
{
    if (!botAI || !npc)
        return false;

    AiObjectContext* context = botAI->GetAiObjectContext();
    bool traded = false;

    if (sPlayerbotAIConfig.coaEconomyRpgSellEnable)
    {
        if (Creature* vendor = AsNpc(npc, UNIT_NPC_FLAG_VENDOR))
            traded |= SellToVendor(botAI, vendor) > 0;

        if (AsNpc(npc, UNIT_NPC_FLAG_REPAIR) && AI_VALUE(bool, "should repair") && AI_VALUE(bool, "can repair"))
            traded |= botAI->DoSpecificAction("repair", Event("rpg action"), true);
    }

    if (AsNpc(npc, UNIT_NPC_FLAG_AUCTIONEER))
        traded |= BotAuctions::RequestVisit(botAI->GetBot(), npc->GetGUID());

    return traded;
}

} // namespace CoaEconomy::RpgTrade
