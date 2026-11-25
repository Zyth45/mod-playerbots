/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

/*
 * CoA World, 3.2.3: where each worn piece came from, for the dashboard's armory (characters.coa_item_origin, form
 * agreed in D:\CoA-Commun\FORME-ITEM-ORIGIN.md). Players and bots alike.
 *  - When an item arrives, its origin is noted in memory: loot (the creature or object; a group roll and the master
 *    looter's gift too), quest, vendor (the NPC),
 *    auction (any win, keyed by the item), dungeonsim (a simulated dungeon's loot, the map).
 *  - When it is worn (one of the 19 equipment slots, green or better), one row is queued: the noted origin, or
 *    "start" for a piece the factory or the auto-gear puts on (CoaWorld::StartGearScope). Anything else (traded,
 *    mailed, an origin not seen) gets no row: the armory shows "Origin unknown". A piece its wearer made keeps no
 *    row (the dashboard reads the creator).
 *  - The queue is written every 60 s in grouped REPLACEs; one CoaBots.log line an hour counts the rows by level band
 *    and source; rows of items gone and rows older than PurgeDays go at start-up and once a day.
 * Only official-core hooks: works the same on the official AzerothCore.
 */

#include "CoaWorldItemOrigin.h"

#include "CoaWorldItemOriginRules.h"

#include "AuctionHouseMgr.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "Item.h"
#include "LootMgr.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "Timer.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
using namespace CoaWorldItemOriginRules;

constexpr char const* PREFIX = "AiPlayerbot.CoaWorld.ItemOrigin.";
constexpr size_t MAX_NOTES = 100000;     // items noted and not worn yet; past it the oldest notes are dropped
constexpr size_t ROWS_PER_STATEMENT = 500;

struct Settings
{
    bool enabled = true;
    uint32 minQuality = 2;
    uint32 purgeDays = 90;
} settings;

void LoadSettings()
{
    settings.enabled = sConfigMgr->GetOption<bool>(std::string(PREFIX) + "Enable", true, false);
    settings.minQuality = std::min<uint32>(5, sConfigMgr->GetOption<uint32>(std::string(PREFIX) + "MinQuality", 2, false));
    settings.purgeDays = sConfigMgr->GetOption<uint32>(std::string(PREFIX) + "PurgeDays", 90, false);
}

struct Note
{
    char const* source;
    uint32 detail;
};

struct Row
{
    uint32 item, owner, entry;
    char const* source;
    uint32 detail;
    uint32 at;
    uint32 band;
};

std::mutex lock;  // the hooks run in the map threads
std::unordered_map<uint32, Note> notes;
std::vector<Row> queue;
HourCounts hour;
uint32 hourRows = 0;
bool tableReady = false;
thread_local uint32 startScopes = 0;  // CoaWorld::StartGearScope: equipping happens in the thread that called it

bool Wanted(ItemTemplate const* proto)
{
    return settings.enabled && proto && ArmoryPiece(proto->Quality, proto->InventoryType, settings.minQuality);
}

void NoteLocked(uint32 item, char const* source, uint32 detail)
{
    if (notes.size() >= MAX_NOTES)
        notes.clear();  // items noted long ago and never worn: sold, destroyed, kept in a bank
    notes[item] = { source, detail };
}

void Queue(Player* player, Item* item, char const* source, uint32 detail)
{
    uint32 const band = LevelBand(player->GetLevel());
    Row row{ uint32(item->GetGUID().GetCounter()), uint32(player->GetGUID().GetCounter()), item->GetEntry(), source,
             detail, uint32(time(nullptr)), band };
    std::lock_guard<std::mutex> guard(lock);
    // The same piece twice in a row (a vendor's piece equipped at once: "start", then "vendor"): the last one counts.
    if (!queue.empty() && queue.back().item == row.item)
    {
        Row const& before = queue.back();
        if (uint32& count = hour[before.band][before.source]; count)
            --count;
        --hourRows;
        queue.back() = row;
    }
    else
        queue.push_back(row);
    ++hour[band][source];
    ++hourRows;
}

void Flush()
{
    std::vector<Row> rows;
    {
        std::lock_guard<std::mutex> guard(lock);
        rows.swap(queue);
    }
    if (rows.empty() || !tableReady)
        return;
    for (size_t i = 0; i < rows.size(); i += ROWS_PER_STATEMENT)
    {
        std::string sql = "REPLACE INTO `coa_item_origin` (`item`, `owner`, `entry`, `source`, `detail`, `at`) VALUES ";
        for (size_t j = i; j < rows.size() && j < i + ROWS_PER_STATEMENT; ++j)
        {
            Row const& r = rows[j];
            sql += (j == i ? "(" : ",(") + std::to_string(r.item) + "," + std::to_string(r.owner) + "," +
                   std::to_string(r.entry) + ",'" + r.source + "'," + std::to_string(r.detail) + "," +
                   std::to_string(r.at) + ")";
        }
        CharacterDatabase.Execute(sql);
    }
}

void EnsureTable()
{
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `coa_item_origin` ("
        "`item` INT UNSIGNED NOT NULL, `owner` INT UNSIGNED NOT NULL, `entry` INT UNSIGNED NOT NULL, "
        "`source` VARCHAR(16) NOT NULL, `detail` INT UNSIGNED NOT NULL DEFAULT 0, `at` INT UNSIGNED NOT NULL, "
        "PRIMARY KEY (`item`), KEY `owner` (`owner`)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
    tableReady = true;
}

// Rows of items that are gone, and rows older than PurgeDays.
void Purge()
{
    if (!tableReady)
        return;
    // A row younger than an hour may be a piece worn and not saved yet (its owner's save comes later): it stays.
    CharacterDatabase.Execute("DELETE o FROM `coa_item_origin` o LEFT JOIN `item_instance` i ON i.`guid` = o.`item` "
                              "WHERE i.`guid` IS NULL AND o.`at` < {}", int64(time(nullptr)) - 3600);
    if (int64 const before = PurgeBefore(int64(time(nullptr)), settings.purgeDays))
        CharacterDatabase.Execute("DELETE FROM `coa_item_origin` WHERE `at` < {}", before);
}

void LogHour()
{
    std::string line;
    uint32 rows = 0;
    {
        std::lock_guard<std::mutex> guard(lock);
        line = HourLine(hour);
        rows = hourRows;
        hour.clear();
        hourRows = 0;
    }
    if (rows)
        LOG_INFO("playerbots.coa", "CoA World: gear (last hour): {} pieces worn; {}", rows, line);
}

class CoaWorldItemOriginPlayer : public PlayerScript
{
public:
    CoaWorldItemOriginPlayer()
        : PlayerScript("CoaWorldItemOriginPlayer", { PLAYERHOOK_ON_EQUIP, PLAYERHOOK_ON_LOOT_ITEM,
                                                     PLAYERHOOK_ON_GROUP_ROLL_REWARD_ITEM, PLAYERHOOK_ON_MASTER_LOOT_ITEM,
                                                     PLAYERHOOK_ON_QUEST_REWARD_ITEM,
                                                     PLAYERHOOK_ON_AFTER_STORE_OR_EQUIP_NEW_ITEM })
    {
    }

    void OnPlayerLootItem(Player* /*player*/, Item* item, uint32 /*count*/, ObjectGuid lootguid) override
    {
        if (item && Wanted(item->GetTemplate()))
        {
            std::lock_guard<std::mutex> guard(lock);
            NoteLocked(item->GetGUID().GetCounter(), "loot", lootguid.GetEntry());
        }
    }

    // 3.3 bench (2 940 self-found bots, 35 min: loot 0): in a group, a piece won at a Need or Greed roll and a piece
    // the master looter gives never pass through OnPlayerLootItem (Group.cpp, LootHandler.cpp). Unnoted, it was worn
    // as "start". Its creature (or 0: a chest) is taken from the roll's loot.
    void OnPlayerGroupRollRewardItem(Player* /*player*/, Item* item, uint32 /*count*/, RollVote /*voteType*/,
                                     Roll* roll) override
    {
        if (!item || !Wanted(item->GetTemplate()))
            return;
        Loot* loot = roll ? roll->getLoot() : nullptr;
        ObjectGuid const source = loot ? loot->sourceWorldObjectGUID : ObjectGuid::Empty;
        std::lock_guard<std::mutex> guard(lock);
        NoteLocked(item->GetGUID().GetCounter(), "loot", source.IsCreature() ? source.GetEntry() : 0);
    }

    void OnPlayerMasterLootItem(Player* /*player*/, Item* item, uint32 /*count*/, ObjectGuid lootguid) override
    {
        if (item && Wanted(item->GetTemplate()))
        {
            std::lock_guard<std::mutex> guard(lock);
            NoteLocked(item->GetGUID().GetCounter(), "loot", lootguid.IsCreature() ? lootguid.GetEntry() : 0);
        }
    }

    void OnPlayerQuestRewardItem(Player* /*player*/, Item* item, uint32 /*count*/) override
    {
        if (item && Wanted(item->GetTemplate()))
        {
            std::lock_guard<std::mutex> guard(lock);
            NoteLocked(item->GetGUID().GetCounter(), "quest", 0);
        }
    }

    void OnPlayerAfterStoreOrEquipNewItem(Player* player, uint32 /*vendorslot*/, Item* item, uint8 /*count*/,
                                          uint8 /*bag*/, uint8 /*slot*/, ItemTemplate const* proto, Creature* vendor,
                                          VendorItem const* /*crItem*/, bool store) override
    {
        if (!item || !Wanted(proto))
            return;
        uint32 const npc = vendor ? vendor->GetEntry() : 0;
        if (!store)
        {
            Queue(player, item, "vendor", npc);  // bought and worn at once
            return;
        }
        std::lock_guard<std::mutex> guard(lock);
        NoteLocked(item->GetGUID().GetCounter(), "vendor", npc);
    }

    void OnPlayerEquip(Player* player, Item* item, uint8 /*bag*/, uint8 slot, bool /*update*/) override
    {
        if (!player || !item || slot >= EQUIPMENT_SLOT_END || !Wanted(item->GetTemplate()))
            return;
        Note note{ nullptr, 0 };
        {
            std::lock_guard<std::mutex> guard(lock);
            auto itr = notes.find(item->GetGUID().GetCounter());
            if (itr != notes.end())
            {
                note = itr->second;
                notes.erase(itr);
            }
        }
        // 3.3 bench: "start" was guessed for any new piece of unseen origin (698 dungeon pieces shown "Starting gear").
        if (char const* source = WornSource(note.source, startScopes > 0,
                                            item->GetGuidValue(ITEM_FIELD_CREATOR) == player->GetGUID()))
            Queue(player, item, source, note.detail);
    }
};

class CoaWorldItemOriginAuction : public AuctionHouseScript
{
public:
    CoaWorldItemOriginAuction()
        : AuctionHouseScript("CoaWorldItemOriginAuction", { AUCTIONHOUSEHOOK_ON_BEFORE_AUCTIONHOUSEMGR_SEND_AUCTION_WON_MAIL })
    {
    }

    // Every win, players' and bots' (a bot's buyout too): the item goes by mail, its guid stays.
    void OnBeforeAuctionHouseMgrSendAuctionWonMail(AuctionHouseMgr* /*mgr*/, AuctionEntry* auction, Player* /*bidder*/,
                                                   uint32& /*bidderAccId*/, bool& /*sendNotification*/,
                                                   bool& /*updateAchievementCriteria*/, bool& /*sendMail*/) override
    {
        if (auction && Wanted(sObjectMgr->GetItemTemplate(auction->item_template)))
        {
            std::lock_guard<std::mutex> guard(lock);
            NoteLocked(auction->item_guid.GetCounter(), "auction", 0);
        }
    }
};

class CoaWorldItemOriginWorld : public WorldScript
{
public:
    CoaWorldItemOriginWorld()
        : WorldScript("CoaWorldItemOriginWorld", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE,
                                                   WORLDHOOK_ON_SHUTDOWN })
    {
    }

    void OnAfterConfigLoad(bool reload) override
    {
        if (reload)
            LoadSettings();
    }

    void OnStartup() override
    {
        LoadSettings();
        EnsureTable();
        Purge();
    }

    void OnShutdown() override { Flush(); }

    void OnUpdate(uint32 diff) override
    {
        _flush += diff;
        _hour += diff;
        _purge += diff;
        if (_flush >= MINUTE * IN_MILLISECONDS)
        {
            _flush = 0;
            Flush();
        }
        if (_hour >= HOUR * IN_MILLISECONDS)
        {
            _hour = 0;
            LogHour();
        }
        if (_purge >= DAY * IN_MILLISECONDS)
        {
            _purge = 0;
            if (settings.enabled)
                Purge();
        }
    }

private:
    uint32 _flush = 0;
    uint32 _hour = 0;
    uint32 _purge = 0;
};
}  // namespace

void CoaWorld::NoteItemOrigin(Item* item, char const* source, uint32 detail)
{
    if (!item || !source || !Wanted(item->GetTemplate()))
        return;
    std::lock_guard<std::mutex> guard(lock);
    NoteLocked(item->GetGUID().GetCounter(), source, detail);
}

CoaWorld::StartGearScope::StartGearScope() { ++startScopes; }
CoaWorld::StartGearScope::~StartGearScope() { --startScopes; }

void AddSC_coa_world_item_origin()
{
    new CoaWorldItemOriginPlayer();
    new CoaWorldItemOriginAuction();
    new CoaWorldItemOriginWorld();
}
