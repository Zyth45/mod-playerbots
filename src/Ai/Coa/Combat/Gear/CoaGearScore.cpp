/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaGearScore.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>

#include "CoaSetBonusRule.h"
#include "DBCStores.h"
#include "Item.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "Playerbots.h"
#include "QuestDef.h"
#include "RandomPlayerbotMgr.h"
#include "StatsWeightCalculator.h"

namespace
{
    // The stock calculator, set as item usage sets it (no stock set bonus, no overflow penalty, PvP weights for a
    // PvP spec), so a score here and a score there are the same number.
    void Configure(StatsWeightCalculator& calculator, Player* bot)
    {
        calculator.SetItemSetBonus(false);
        calculator.SetOverflowPenalty(false);
        if (sRandomPlayerbotMgr.IsSpecPvp(bot->GetGUID().GetCounter(), bot->getClass()))
            calculator.SetPvpSpec(true);
    }

    // Pieces of `setId` worn in the other equipment slots (all slots but `excludedSlot`).
    uint32 OthersWorn(Player* bot, uint32 setId, int32 excludedSlot)
    {
        uint32 count = 0;
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            if (int32(slot) == excludedSlot)
                continue;
            Item* worn = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (worn && worn->GetTemplate() && worn->GetTemplate()->ItemSet == setId)
                ++count;
        }
        return count;
    }

    // Token -> the quests' gear rewards and their class masks, built once from the quest templates.
    struct TokenReward
    {
        uint32 itemId;
        uint32 questClasses;  // 0: every class
    };

    std::once_flag TokenIndexOnce;
    std::unordered_map<uint32, std::vector<TokenReward>> TokenIndex;

    bool IsGear(ItemTemplate const* proto)
    {
        return proto && (proto->Class == ITEM_CLASS_ARMOR || proto->Class == ITEM_CLASS_WEAPON) &&
               proto->InventoryType != INVTYPE_NON_EQUIP;
    }

    void BuildTokenIndex()
    {
        for (auto const& [questId, quest] : sObjectMgr->GetQuestTemplates())
        {
            if (!quest)
                continue;
            std::vector<uint32> rewards;
            for (uint32 id : quest->RewardItemId)
                if (IsGear(sObjectMgr->GetItemTemplate(id)))
                    rewards.push_back(id);
            for (uint32 id : quest->RewardChoiceItemId)
                if (IsGear(sObjectMgr->GetItemTemplate(id)))
                    rewards.push_back(id);
            if (rewards.empty())
                continue;
            // The token is the rare-or-better non-gear item the quest takes (the bindings, the bijou); scarabs and
            // coins are common materials and do not make a token.
            for (uint32 required : quest->RequiredItemId)
            {
                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(required);
                if (!proto || IsGear(proto) || proto->Quality < ITEM_QUALITY_RARE)
                    continue;
                for (uint32 reward : rewards)
                    TokenIndex[required].push_back({reward, quest->GetRequiredClasses()});
            }
        }
    }

    std::unordered_map<uint32, std::vector<TokenReward>> const& Tokens()
    {
        std::call_once(TokenIndexOnce, BuildTokenIndex);
        return TokenIndex;
    }
}

float CoaGearScore::SetBonusMarginal(Player* bot, ItemTemplate const* proto, int32_t equipSlot)
{
    if (!sPlayerbotAIConfig.coaCombatGearScoreEnable || !bot || !proto || !proto->ItemSet)
        return 0.0f;
    ItemSetEntry const* set = sItemSetStore.LookupEntry(proto->ItemSet);
    if (!set)
        return 0.0f;

    StatsWeightCalculator calculator(bot);
    Configure(calculator, bot);
    // A bonus is scored in the item scores' unit: its stats' weight times the piece's item-level and quality blend.
    float const blend = float(PlayerbotFactory::CalcMixedGearScore(proto->ItemLevel, proto->Quality));

    std::vector<CoaSetBonusRule::Bonus> bonuses;
    for (uint32 i = 0; i < MAX_ITEM_SET_SPELLS; ++i)
        if (set->spells[i] && set->items_to_triggerspell[i])
            bonuses.push_back({set->items_to_triggerspell[i], calculator.CalculateSpell(set->spells[i]) * blend});

    return CoaSetBonusRule::Marginal(OthersWorn(bot, proto->ItemSet, equipSlot), bonuses);
}

float CoaGearScore::ItemScore(Player* bot, uint32_t itemId, int32_t randomPropertyId, int32_t equipSlot)
{
    ItemTemplate const* proto = bot ? sObjectMgr->GetItemTemplate(itemId) : nullptr;
    if (!proto)
        return 0.0f;

    StatsWeightCalculator calculator(bot);
    Configure(calculator, bot);
    float score = calculator.CalculateItem(itemId, randomPropertyId);

    if (equipSlot < 0)
        if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
        {
            uint8 const slot = botAI->FindEquipSlot(proto, NULL_SLOT, true);
            equipSlot = slot == NULL_SLOT ? -1 : int32(slot);
        }
    return score + SetBonusMarginal(bot, proto, equipSlot);
}

float CoaGearScore::UpgradeGain(Player* bot, uint32_t itemId, int32_t randomPropertyId)
{
    ItemTemplate const* proto = bot ? sObjectMgr->GetItemTemplate(itemId) : nullptr;
    PlayerbotAI* botAI = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
    if (!proto || !botAI || !IsGear(proto) || bot->BotCanUseItem(proto) != EQUIP_ERR_OK)
        return 0.0f;

    uint8 const first = botAI->FindEquipSlot(proto, NULL_SLOT, true);
    if (first == NULL_SLOT)
        return 0.0f;
    std::vector<uint8> slots{first};
    if (first == EQUIPMENT_SLOT_FINGER1 || first == EQUIPMENT_SLOT_FINGER2)
        slots = {EQUIPMENT_SLOT_FINGER1, EQUIPMENT_SLOT_FINGER2};
    else if (first == EQUIPMENT_SLOT_TRINKET1 || first == EQUIPMENT_SLOT_TRINKET2)
        slots = {EQUIPMENT_SLOT_TRINKET1, EQUIPMENT_SLOT_TRINKET2};

    float best = 0.0f;
    for (uint8 slot : slots)
    {
        Item* worn = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (worn && worn->GetEntry() == itemId)
            return 0.0f;  // already worn
        float const candidate = ItemScore(bot, itemId, randomPropertyId, slot);
        float const current = worn ? ItemScore(bot, worn->GetEntry(), worn->GetInt32Value(ITEM_FIELD_RANDOM_PROPERTIES_ID),
                                               slot)
                                   : 0.0f;
        best = std::max(best, candidate - current);
    }
    return best;
}

bool CoaGearScore::IsToken(uint32_t itemId)
{
    return Tokens().count(itemId) != 0;
}

std::vector<uint32_t> CoaGearScore::TokenRewardsFor(Player* bot, uint32_t tokenId)
{
    std::vector<uint32_t> out;
    if (!bot)
        return out;
    auto itr = Tokens().find(tokenId);
    if (itr == Tokens().end())
        return out;
    uint32 const classMask = bot->getClassMask();
    for (TokenReward const& r : itr->second)
    {
        if (r.questClasses && !(r.questClasses & classMask))
            continue;
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(r.itemId);
        if (!proto || (proto->AllowableClass && !(proto->AllowableClass & int32(classMask))))
            continue;
        if (std::find(out.begin(), out.end(), r.itemId) == out.end())
            out.push_back(r.itemId);
    }
    return out;
}
