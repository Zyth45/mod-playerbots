/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaGearPvp.h"

#include <mutex>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "CoaGearPvpRule.h"
#include "DatabaseEnv.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "QueryResult.h"
#include "Timer.h"

namespace
{
    // Every table that gives a player an item, with its item column (docs/ETUDE-EQUIPEMENT-PVP.md). The Manastorm
    // demon's random pool (manastorm_greedy_demon_loot_pool) is left out on purpose: it holds nearly every item,
    // the Grand Marshal's and High Warlord's sets included (user, 14/11: "Retirer GM/HW").
    struct SourceColumn
    {
        char const* table;
        char const* column;
        char const* where;
    };

    SourceColumn const SOURCES[] = {
        {"npc_vendor", "item", ""},
        {"game_event_npc_vendor", "item", ""},
        {"creature_loot_template", "Item", "Reference = 0"},
        {"gameobject_loot_template", "Item", "Reference = 0"},
        {"reference_loot_template", "Item", "Reference = 0"},
        {"item_loot_template", "Item", "Reference = 0"},
        {"mail_loot_template", "Item", "Reference = 0"},
        {"player_loot_template", "Item", "Reference = 0"},
        {"pickpocketing_loot_template", "Item", "Reference = 0"},
        {"skinning_loot_template", "Item", "Reference = 0"},
        {"disenchant_loot_template", "Item", "Reference = 0"},
        {"prospecting_loot_template", "Item", "Reference = 0"},
        {"milling_loot_template", "Item", "Reference = 0"},
        {"fishing_loot_template", "Item", "Reference = 0"},
        {"quest_template", "RewardItem1", ""},
        {"quest_template", "RewardItem2", ""},
        {"quest_template", "RewardItem3", ""},
        {"quest_template", "RewardItem4", ""},
        {"quest_template", "RewardChoiceItemID1", ""},
        {"quest_template", "RewardChoiceItemID2", ""},
        {"quest_template", "RewardChoiceItemID3", ""},
        {"quest_template", "RewardChoiceItemID4", ""},
        {"quest_template", "RewardChoiceItemID5", ""},
        {"quest_template", "RewardChoiceItemID6", ""},
        {"achievement_reward", "ItemID", ""},
        {"ascension_cache_content_token", "RewardItemId", ""},
        {"ascension_callboard_cache_reward", "RewardItemId", ""},
        {"ascension_prestigious_cache_reward", "RewardItemId", ""},
        {"coa_bloodforged_catalogue", "entry", ""},
        {"coa_challenge_reward", "itemId", ""},
        {"coa_crows_cache_rewards", "item", ""},
        {"coa_mc_fire_lord_cache_pool", "ItemEntry", ""},
        {"coa_mc_item_pool", "ItemEntry", ""},
        {"coa_mythic_reward", "item", ""},
        {"ethereal_bazaar_cache_pool", "reward_item", ""},
        {"ethereal_bazaar_pool", "item", ""},
        {"ethereal_bazaar_set_cache", "member_item", ""},
        {"manastorm_material_cache_pool", "item_entry", ""},
        {"manastorm_potential_reward", "item_entry", ""},
        {"manastorm_reward_rule", "item_entry", ""},
        {"recovery_worldforged_offer", "refunded_item", ""},
        {"recruit_milestone_rewards", "item_id", ""},
        {"recruit_rewards", "item_id", ""},
    };

    // (from, to): `to` is obtainable when `from` is.
    SourceColumn const DERIVED[] = {
        {"coa_dungeon_loot_variant", "base_item", "heroic_item"},
        {"coa_dungeon_loot_variant", "base_item", "mythic_item"},
        {"worldforged_upgrade", "base_item", "target_item"},
    };

    bool TableExists(char const* table)
    {
        return WorldDatabase.Query(
                   "SELECT 1 FROM information_schema.tables WHERE table_schema = DATABASE() AND table_name = '{}'",
                   table) != nullptr;
    }

    std::unordered_set<uint32> Compute()
    {
        uint32 const startMs = getMSTime();
        std::unordered_set<uint32_t> sources;
        uint32 missingTables = 0;
        for (SourceColumn const& s : SOURCES)
        {
            if (!TableExists(s.table))
            {
                ++missingTables;
                continue;
            }
            std::string sql = std::string("SELECT DISTINCT `") + s.column + "` FROM `" + s.table + "`";
            if (*s.where)
                sql += std::string(" WHERE ") + s.where;
            if (QueryResult result = WorldDatabase.Query(sql))
                do
                    sources.insert(result->Fetch()[0].Get<uint32>());
                while (result->NextRow());
        }

        std::vector<std::pair<uint32_t, uint32_t>> derived;
        for (SourceColumn const& d : DERIVED)
        {
            if (!TableExists(d.table))
                continue;
            std::string const sql =
                std::string("SELECT `") + d.column + "`, `" + d.where + "` FROM `" + d.table + "`";
            if (QueryResult result = WorldDatabase.Query(sql))
                do
                    derived.emplace_back(result->Fetch()[0].Get<uint32>(), result->Fetch()[1].Get<uint32>());
                while (result->NextRow());
        }

        std::vector<CoaGearPvpRule::Item> items;
        for (auto const& [id, proto] : *sObjectMgr->GetItemTemplateStore())
        {
            CoaGearPvpRule::Item item;
            item.id = id;
            item.name = proto.Name1;
            item.inventoryType = proto.InventoryType;
            bool resilience = false;
            for (uint32 i = 0; i < MAX_ITEM_PROTO_STATS; ++i)
                if (proto.ItemStat[i].ItemStatType == ITEM_MOD_RESILIENCE_RATING)
                    resilience = true;
            item.pvp = (proto.Class == ITEM_CLASS_WEAPON || proto.Class == ITEM_CLASS_ARMOR) &&
                       (resilience || proto.RequiredHonorRank > 0);
            items.push_back(std::move(item));
        }

        std::unordered_set<uint32_t> const out = CoaGearPvpRule::Unobtainable(items, std::move(sources), derived);
        LOG_INFO("playerbots",
                 "CoA PvP gear: {} PvP pieces with no vendor, quest, loot or reward, kept off bots ({} source tables "
                 "missing, {} ms)",
                 out.size(), missingTables, GetMSTimeDiffToNow(startMs));
        return {out.begin(), out.end()};
    }

    std::once_flag loadOnce;
    std::unordered_set<uint32> unobtainable;
}

void CoaGearPvp::Load()
{
    std::call_once(loadOnce, [] { unobtainable = Compute(); });
}

bool CoaGearPvp::IsUnobtainablePvpItem(uint32 itemId)
{
    Load();
    return unobtainable.count(itemId) != 0;
}
