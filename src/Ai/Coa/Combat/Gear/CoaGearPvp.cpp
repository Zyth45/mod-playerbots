/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaGearPvp.h"

#include <unordered_set>

#include "DatabaseEnv.h"
#include "QueryResult.h"
#include "Log.h"

namespace
{
    std::unordered_set<uint32> LoadUnobtainablePvpItems()
    {
        std::unordered_set<uint32> items;
        // Resilience is stat type 35. A source: any vendor, any quest reward or choice, any loot table entry.
        QueryResult result = WorldDatabase.Query(
            "SELECT t.entry FROM item_template t WHERE (t.RequiredHonorRank > 0 OR 35 IN (t.stat_type1, t.stat_type2, "
            "t.stat_type3, t.stat_type4, t.stat_type5, t.stat_type6, t.stat_type7, t.stat_type8, t.stat_type9, "
            "t.stat_type10)) AND t.entry NOT IN ("
            "SELECT item FROM npc_vendor "
            "UNION SELECT Item FROM creature_loot_template WHERE Reference = 0 "
            "UNION SELECT Item FROM gameobject_loot_template WHERE Reference = 0 "
            "UNION SELECT Item FROM reference_loot_template WHERE Reference = 0 "
            "UNION SELECT Item FROM item_loot_template WHERE Reference = 0 "
            "UNION SELECT RewardItem1 FROM quest_template UNION SELECT RewardItem2 FROM quest_template "
            "UNION SELECT RewardItem3 FROM quest_template UNION SELECT RewardItem4 FROM quest_template "
            "UNION SELECT RewardChoiceItemID1 FROM quest_template UNION SELECT RewardChoiceItemID2 FROM quest_template "
            "UNION SELECT RewardChoiceItemID3 FROM quest_template UNION SELECT RewardChoiceItemID4 FROM quest_template "
            "UNION SELECT RewardChoiceItemID5 FROM quest_template UNION SELECT RewardChoiceItemID6 FROM quest_template)");
        if (result)
            do
                items.insert(result->Fetch()[0].Get<uint32>());
            while (result->NextRow());
        LOG_INFO("playerbots", "CoA PvP gear: {} PvP items with no vendor, quest or loot, kept off bots", items.size());
        return items;
    }
}

bool CoaGearPvp::IsUnobtainablePvpItem(uint32 itemId)
{
    static std::unordered_set<uint32> const items = LoadUnobtainablePvpItems();
    return items.count(itemId) != 0;
}
