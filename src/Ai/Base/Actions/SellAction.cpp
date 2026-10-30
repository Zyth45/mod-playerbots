/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "SellAction.h"
#include "CoaMissionMgr.h"
#include "ChatHelper.h"
#include "Event.h"
#include "ItemPackets.h"
#include "ItemUsageValue.h"
#include "ItemVisitors.h"
#include "Playerbots.h"

class SellItemsVisitor : public IterateItemsVisitor
{
public:
    SellItemsVisitor(SellAction* action) : IterateItemsVisitor(), action(action) {}

    bool Visit(Item* item) override
    {
        action->Sell(item);
        return true;
    }

private:
    SellAction* action;
};

class SellQualityItemsVisitor : public SellItemsVisitor
{
public:
    SellQualityItemsVisitor(SellAction* action, uint32 maxQuality, bool allClasses)
        : SellItemsVisitor(action), maxQuality(maxQuality), allClasses(allClasses)
    {
    }

    bool Visit(Item* item) override
    {
        ItemTemplate const* proto = item->GetTemplate();
        if (proto->Quality > maxQuality)
            return true;

        if (IsProfessionTool(proto))
            return true;

        if (!allClasses && proto->Quality > ITEM_QUALITY_POOR && !IsEquipment(proto))
            return true;

        return SellItemsVisitor::Visit(item);
    }

private:
    static bool IsEquipment(ItemTemplate const* proto)
    {
        return proto->Class == ITEM_CLASS_ARMOR || proto->Class == ITEM_CLASS_WEAPON;
    }

    static bool IsProfessionTool(ItemTemplate const* proto)
    {
        if (proto->Class != ITEM_CLASS_WEAPON)
            return false;

        if (proto->SubClass == ITEM_SUBCLASS_WEAPON_MISC || proto->SubClass == ITEM_SUBCLASS_WEAPON_FISHING_POLE)
            return true;

        return proto->TotemCategory != 0;
    }

    uint32 maxQuality;
    bool allClasses;
};

class SellVendorItemsVisitor : public SellItemsVisitor
{
public:
    SellVendorItemsVisitor(SellAction* action, AiObjectContext* con) : SellItemsVisitor(action) { context = con; }

    AiObjectContext* context;

    bool Visit(Item* item) override
    {
        ItemUsage usage = context->GetValue<ItemUsage>("item usage", item->GetEntry())->Get();
        if (usage != ITEM_USAGE_VENDOR && usage != ITEM_USAGE_AH)
            return true;

        return SellItemsVisitor::Visit(item);
    }
};

class SellJunkItemsVisitor : public SellItemsVisitor
{
public:
    SellJunkItemsVisitor(SellAction* action, AiObjectContext* con) : SellItemsVisitor(action), context(con) {}

    bool Visit(Item* item) override
    {
        ItemTemplate const* proto = item->GetTemplate();
        if (proto->Quality > ITEM_QUALITY_UNCOMMON || proto->StartQuest || proto->Bonding == BIND_QUEST_ITEM)
            return true;

        if (proto->Class == ITEM_CLASS_WEAPON && (proto->SubClass == ITEM_SUBCLASS_WEAPON_MISC ||
            proto->SubClass == ITEM_SUBCLASS_WEAPON_FISHING_POLE || proto->TotemCategory != 0))
            return true;

        if (proto->Quality == ITEM_QUALITY_POOR)
            return SellItemsVisitor::Visit(item);

        if (proto->Class != ITEM_CLASS_ARMOR && proto->Class != ITEM_CLASS_WEAPON)
            return true;

        ItemUsage usage = context->GetValue<ItemUsage>("item usage", item->GetEntry())->Get();
        if (usage != ITEM_USAGE_NONE && usage != ITEM_USAGE_VENDOR && usage != ITEM_USAGE_AH && usage != ITEM_USAGE_BAD_EQUIP)
            return true;

        return SellItemsVisitor::Visit(item);
    }

private:
    AiObjectContext* context;
};

bool SellAction::Execute(Event event)
{
    // A bot on a gathering mission keeps what it gathers for its owner: no selling.
    if (sCoaMissionMgr.IsOnMission(bot))
    {
        botAI->TellMasterNoFacing("I do not sell during a mission.");
        return false;
    }

    std::string const text = event.getParam();
    batch = text.find("Hitem:") == std::string::npos;
    soldCount = 0;
    soldMoney = 0;
    if (text == "gray" || text == "*")
    {
        SellQualityItemsVisitor visitor(this, ITEM_QUALITY_POOR, false);
        IterateItems(&visitor);
        TellSummary();
        return true;
    }

    if (text == "vendor")
    {
        SellVendorItemsVisitor visitor(this, context);
        IterateItems(&visitor);
        TellSummary();
        return true;
    }

    if (text == "junk")
    {
        SellJunkItemsVisitor visitor(this, context);
        IterateItems(&visitor);
        TellSummary();
        return true;
    }

    std::string quality = text;
    bool allClasses = false;

    size_t const split = quality.rfind(' ');
    if (split != std::string::npos && quality.substr(split + 1) == "all")
    {
        quality.erase(split);
        allClasses = true;
    }

    uint32 const maxQuality = ChatHelper::parseItemQuality(quality);
    if (maxQuality != MAX_ITEM_QUALITY)
    {
        SellQualityItemsVisitor visitor(this, maxQuality, allClasses);
        IterateItems(&visitor);
        TellSummary();
        return true;
    }

    if (text != "")
    {
        std::vector<Item*> items = parseItems(text, ITERATE_ITEMS_IN_BAGS);
        for (Item* item : items)
        {
            Sell(item);
        }
        return true;
    }

    botAI->TellError("Usage: s gray/*/junk/vendor/<quality> [all]/[item link]");
    return false;
}

void SellAction::Sell(FindItemVisitor* visitor)
{
    IterateItems(visitor);
    std::vector<Item*> items = visitor->GetResult();
    for (Item* item : items)
    {
        Sell(item);
    }
}

void SellAction::Sell(Item* item)
{
    std::ostringstream out;

    GuidVector vendors = botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest npcs")->Get();

    for (ObjectGuid const vendorguid : vendors)
    {
        Creature* pCreature = bot->GetNPCIfCanInteractWith(vendorguid, UNIT_NPC_FLAG_VENDOR);
        if (!pCreature)
            continue;

        ObjectGuid itemguid = item->GetGUID();
        uint32 count = item->GetCount();

        uint32 botMoney = bot->GetMoney();

        WorldPacket p(CMSG_SELL_ITEM);
        p << vendorguid << itemguid << count;

        WorldPackets::Item::SellItem nicePacket(std::move(p));
        nicePacket.Read();
        bot->GetSession()->HandleSellItemOpcode(nicePacket);

        ++soldCount;
        if (bot->GetMoney() > botMoney)
            soldMoney += bot->GetMoney() - botMoney;

        if (!batch)
        {
            out << "Selling " << chat->FormatItem(item->GetTemplate());
            botAI->TellMaster(out);
        }

        if (botAI->HasCheat(BotCheatMask::gold))
            bot->SetMoney(botMoney);

        bot->PlayDistanceSound(120);
        break;
    }
}

void SellAction::TellSummary()
{
    if (!soldCount)
    {
        botAI->TellMaster("Nothing to sell here (or no vendor next to me).");
        return;
    }

    std::ostringstream out;
    out << "Sold " << soldCount << (soldCount > 1 ? " items for " : " item for ") << ChatHelper::formatMoney(soldMoney);
    botAI->TellMaster(out);
}
