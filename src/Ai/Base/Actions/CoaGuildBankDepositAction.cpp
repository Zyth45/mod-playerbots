/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaGuildBankDepositAction.h"

#include "Bag.h"
#include "CellImpl.h"
#include "CharacterCache.h"
#include "CoaMissionMgr.h"
#include "Event.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Item.h"
#include "NearestGameObjects.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotOperation.h"
#include "PlayerbotWorldThreadProcessor.h"
#include "Playerbots.h"

#include <algorithm>
#include <ctime>
#include <list>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>

namespace
{
    // Distance to the vault at which the bot deposits (a bot following its master stands a few yards behind).
    constexpr float GUILD_BANK_RANGE = 12.0f;
    // Seconds between two looks for a vault, and after a deposit was queued.
    constexpr time_t CHECK_SECONDS = 10;
    constexpr time_t AFTER_DEPOSIT_SECONDS = 60;
    // Stacks moved in one visit at most (each one is a database transaction).
    constexpr uint32 MAX_STACKS_PER_VISIT = 40;

    uint32 KeptMoney()
    {
        return uint32(std::min<uint64>(uint64(sPlayerbotAIConfig.guildBankDepositKeepGold) * GOLD, MAX_MONEY_AMOUNT));
    }

    bool CanDepositNow(Player* bot)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive() || bot->IsInCombat() || bot->IsBeingTeleported())
            return false;
        if (!bot->GetMap() || bot->GetMap()->Instanceable())
            return false;
        if (sCoaMissionMgr.IsOnMission(bot))
            return false;
        Guild* guild = sGuildMgr->GetGuildById(bot->GetGuildId());
        return guild && CoaGuildBankDepositAction::IsRealPlayerGuild(guild);
    }

    // Runs in the world thread, like the guild bank packets of a player: the guild bank is not thread-safe.
    class CoaGuildBankDepositOperation : public PlayerbotOperation
    {
    public:
        CoaGuildBankDepositOperation(ObjectGuid botGuid, ObjectGuid bankGuid) : m_botGuid(botGuid), m_bankGuid(bankGuid) {}

        ObjectGuid GetBotGuid() const override { return m_botGuid; }
        std::string GetName() const override { return "CoaGuildBankDeposit"; }

        bool Execute() override
        {
            if (!sPlayerbotAIConfig.guildBankDepositEnable)
                return false;

            Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
            if (!CanDepositNow(bot))
                return false;
            PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
            if (!botAI)
                return false;

            Guild* guild = sGuildMgr->GetGuildById(bot->GetGuildId());
            GameObject* bank = bot->GetMap()->GetGameObject(m_bankGuid);
            if (!guild || !bank || bank->GetGoType() != GAMEOBJECT_TYPE_GUILD_BANK ||
                !bot->IsWithinDistInMap(bank, GUILD_BANK_RANGE))
                return false;

            uint32 stacks = DepositItems(bot, guild);
            uint32 money = DepositMoney(bot, guild);
            if (!stacks && !money)
                return false;

            std::ostringstream out;
            out << "Guild bank: deposited";
            if (stacks)
                out << " " << stacks << (stacks == 1 ? " stack" : " stacks");
            if (stacks && money)
                out << " and";
            if (money)
                out << " " << money / GOLD << "g " << (money % GOLD) / SILVER << "s";
            botAI->TellMasterNoFacing(out.str());
            LOG_DEBUG("playerbots", "Bot {} {} (guild {})", bot->GetName(), out.str(), guild->GetName());
            return true;
        }

    private:
        static uint32 DepositItems(Player* bot, Guild* guild)
        {
            // Tabs where the rank of the bot may put items: the configured one, or all of them in order.
            std::vector<uint8> tabs;
            uint32 const wanted = sPlayerbotAIConfig.guildBankDepositTab;
            for (uint8 tab = 0; tab < GUILD_BANK_MAX_TABS; ++tab)
            {
                if (wanted && tab != wanted - 1)
                    continue;
                if (guild->MemberHasTabRights(bot->GetGUID(), tab, GUILD_BANK_RIGHT_DEPOSIT_ITEM))
                    tabs.push_back(tab);
            }
            if (tabs.empty())
                return 0;

            // Backpack and the four bags only: never the equipped items, the bank or the keyring.
            std::vector<std::pair<uint8, uint8>> positions;
            for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
                if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                    if (CoaGuildBankDepositAction::IsDepositable(bot, item))
                        positions.emplace_back(INVENTORY_SLOT_BAG_0, slot);
            for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
                if (Bag* bag = bot->GetBagByPos(bagSlot))
                    for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                        if (Item* item = bag->GetItemByPos(uint8(slot)))
                            if (CoaGuildBankDepositAction::IsDepositable(bot, item))
                                positions.emplace_back(bagSlot, uint8(slot));

            uint32 moved = 0;
            for (auto const& [bag, slot] : positions)
            {
                if (moved >= MAX_STACKS_PER_VISIT)
                    break;
                for (uint8 tab : tabs)
                {
                    // NULL_SLOT: the core merges with the stacks of the tab, else takes a free slot, else
                    // leaves the whole stack in the bag (tab full, tab not bought, no right).
                    guild->SwapItemsWithInventory(bot, false, tab, NULL_SLOT, bag, slot, 0);
                    if (!bot->GetItemByPos(bag, slot))
                    {
                        ++moved;
                        break;
                    }
                }
            }
            return moved;
        }

        static uint32 DepositMoney(Player* bot, Guild* guild)
        {
            uint32 const before = bot->GetMoney();
            uint32 const kept = KeptMoney();
            if (before <= kept)
                return 0;
            guild->HandleMemberDepositMoney(bot->GetSession(), before - kept);
            uint32 const after = bot->GetMoney();
            return after < before ? before - after : 0;
        }

        ObjectGuid m_botGuid;
        ObjectGuid m_bankGuid;
    };
}

bool CoaGuildBankDepositAction::IsRealPlayerGuild(Guild const* guild)
{
    if (!guild)
        return false;
    uint32 const account = sCharacterCache->GetCharacterAccountIdByGuid(guild->GetLeaderGUID());
    return account && !sPlayerbotAIConfig.IsInRandomAccountList(account);
}

bool CoaGuildBankDepositAction::IsDepositable(Player* bot, Item const* item)
{
    if (!item)
        return false;
    ItemTemplate const* proto = item->GetTemplate();
    if (!proto)
        return false;

    if (item->IsSoulBound() || item->IsInTrade() || item->IsNotEmptyBag() || !item->CanBeTraded())
        return false;
    if (proto->Class == ITEM_CLASS_QUEST || proto->StartQuest || proto->Bonding == BIND_QUEST_ITEM ||
        proto->Bonding == BIND_QUEST_ITEM1 || proto->Duration > 0 || proto->HasFlag(ITEM_FLAG_CONJURED))
        return false;
    if (item->GetCount() < std::max<uint32>(1, sPlayerbotAIConfig.guildBankDepositMinStack))
        return false;

    for (auto const& [itemClass, itemSubClass] : sPlayerbotAIConfig.guildBankDepositItems)
    {
        // "boe": bind-on-equip gear (green or better) this alt cannot wear, for a guildmate who can.
        if (itemClass == GUILD_DEPOSIT_BOE)
        {
            if (proto->Bonding == BIND_WHEN_EQUIPPED && proto->Quality >= ITEM_QUALITY_UNCOMMON &&
                (proto->Class == ITEM_CLASS_ARMOR || proto->Class == ITEM_CLASS_WEAPON) &&
                bot && bot->CanUseItem(proto) != EQUIP_ERR_OK)
                return true;
            continue;
        }
        if (int32(proto->Class) == itemClass && (itemSubClass < 0 || int32(proto->SubClass) == itemSubClass))
            return true;
    }
    return false;
}

bool CoaGuildBankDepositAction::HasSomethingToDeposit(Player* bot)
{
    if (bot->GetMoney() > KeptMoney())
        return true;

    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (IsDepositable(bot, bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot)))
            return true;
    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
        if (Bag* bag = bot->GetBagByPos(bagSlot))
            for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                if (IsDepositable(bot, bag->GetItemByPos(uint8(slot))))
                    return true;
    return false;
}

GameObject* CoaGuildBankDepositAction::FindGuildBank(Player* bot)
{
    std::list<GameObject*> targets;
    AnyGameObjectInObjectRangeCheck check(bot, GUILD_BANK_RANGE);
    Acore::GameObjectListSearcher<AnyGameObjectInObjectRangeCheck> searcher(bot, targets, check);
    Cell::VisitObjects(bot, searcher, GUILD_BANK_RANGE);

    for (GameObject* go : targets)
    {
        // A spawned vault of a city; the personal vaults of CoA are summoned (no spawn id).
        if (go->GetGoType() == GAMEOBJECT_TYPE_GUILD_BANK && go->GetSpawnId())
            return go;
    }
    return nullptr;
}

bool CoaGuildBankDepositAction::isUseful()
{
    return sPlayerbotAIConfig.guildBankDepositEnable && bot->GetGuildId() && time(nullptr) >= nextCheck;
}

bool CoaGuildBankDepositAction::Execute(Event /*event*/)
{
    time_t const now = time(nullptr);
    nextCheck = now + CHECK_SECONDS;

    if (!CanDepositNow(bot) || !HasSomethingToDeposit(bot))
        return false;

    GameObject* bank = FindGuildBank(bot);
    if (!bank)
        return false;

    auto operation = std::make_unique<CoaGuildBankDepositOperation>(bot->GetGUID(), bank->GetGUID());
    if (!PlayerbotWorldThreadProcessor::instance().QueueOperation(std::move(operation)))
        return false;

    nextCheck = now + AFTER_DEPOSIT_SECONDS;
    return true;
}
