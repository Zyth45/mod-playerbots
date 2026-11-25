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
 *    mailed, an origin not seen) gets no row: the armory shows "Origin unknown". 3.3.1: a piece with a maker
 *    (crafted, or signed) is "selfcraft" on its maker and "craft" on anyone else.
 *  - The queue is written every 60 s in grouped REPLACEs; one CoaBots.log line an hour counts the rows by level band
 *    and source; rows of items gone and rows older than PurgeDays go at start-up and once a day.
 * 3.3.1: the last events of each piece, for the dashboard's character sheet (characters.coa_item_event, the last 20
 * per character, form in D:\CoA-Commun\tmp\coa_item_event-schema.md):
 *  - "equip": every time a piece (green or better) is worn, its source as above ("" when not seen: the dashboard
 *    reads coa_item_origin); the same piece worn again within a minute writes nothing. Not at login.
 *  - "enchant": an enchantment put on a piece, its id in detail: "spell" (an enchanting spell or scroll cast by a
 *    player or a bot), "autogear" (the factory), "bot" (a bot's command), "artisan" (a bot enchanter, Economy).
 *    The CoA mystic enchantments are not covered.
 *  - "craft"/"selfcraft": a piece made with a profession spell (OnPlayerCreateItem) or by a bot's craft
 *    (CoaWorld::NoteCraft), or signed by a maker: "selfcraft" on its maker, "craft" on anyone else.
 *  - Written with the origins every 60 s; each character's events past the 20 last go once an hour.
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
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
using namespace CoaWorldItemOriginRules;

constexpr char const* PREFIX = "AiPlayerbot.CoaWorld.ItemOrigin.";
constexpr char const* EVENT_PREFIX = "AiPlayerbot.CoaWorld.ItemEvent.";
constexpr size_t MAX_WORN = 200000;      // pieces whose last "equip" event is remembered (the one-minute rule)
constexpr size_t MAX_NOTES = 100000;     // items noted and not worn yet; past it the oldest notes are dropped
constexpr size_t ROWS_PER_STATEMENT = 500;

struct Settings
{
    bool enabled = true;
    bool events = true;
    uint32 minQuality = 2;
    uint32 purgeDays = 90;
} settings;

void LoadSettings()
{
    settings.enabled = sConfigMgr->GetOption<bool>(std::string(PREFIX) + "Enable", true, false);
    settings.minQuality = std::min<uint32>(5, sConfigMgr->GetOption<uint32>(std::string(PREFIX) + "MinQuality", 2, false));
    settings.purgeDays = sConfigMgr->GetOption<uint32>(std::string(PREFIX) + "PurgeDays", 90, false);
    settings.events = sConfigMgr->GetOption<bool>(std::string(EVENT_PREFIX) + "Enable", true, false);
}

struct Note
{
    char const* source;
    uint32 detail;
    uint32 maker;  // the crafter's guid for "craft", else 0
};

struct Row
{
    uint32 item, owner, entry;
    char const* source;
    uint32 detail;
    uint32 at;
    uint32 band;
};

struct Event
{
    uint32 owner, item, entry;
    char const* kind;
    char const* source;
    uint32 detail;
    uint32 at;
};

struct Worn
{
    char const* source;  // the source of its last "equip" event, nullptr when not seen
    uint32 at;
};

std::mutex lock;  // the hooks run in the map threads
std::unordered_map<uint32, Note> notes;
std::vector<Row> queue;
std::vector<Event> events;
std::unordered_map<uint32, Worn> worn;  // by item
HourCounts hour;
uint32 hourRows = 0;
bool tableReady = false;
thread_local uint32 startScopes = 0;  // CoaWorld::StartGearScope: equipping happens in the thread that called it

// An item whose origin is noted: wanted by coa_item_origin or by coa_item_event.
bool Wanted(ItemTemplate const* proto)
{
    return (settings.enabled || settings.events) && proto &&
           ArmoryPiece(proto->Quality, proto->InventoryType, settings.minQuality);
}

void NoteLocked(uint32 item, char const* source, uint32 detail, uint32 maker = 0)
{
    if (notes.size() >= MAX_NOTES)
        notes.clear();  // items noted long ago and never worn: sold, destroyed, kept in a bank
    notes[item] = { source, detail, maker };
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

void QueueEventLocked(uint32 owner, Item* item, char const* kind, char const* source, uint32 detail, uint32 at)
{
    events.push_back({ owner, uint32(item->GetGUID().GetCounter()), item->GetEntry(), kind, source ? source : "",
                       detail, at });
}

void FlushEvents(std::vector<Event> const& rows)
{
    for (size_t i = 0; i < rows.size(); i += ROWS_PER_STATEMENT)
    {
        std::string sql =
            "INSERT INTO `coa_item_event` (`owner`, `item`, `entry`, `kind`, `source`, `detail`, `at`) VALUES ";
        for (size_t j = i; j < rows.size() && j < i + ROWS_PER_STATEMENT; ++j)
        {
            Event const& e = rows[j];
            sql += (j == i ? "(" : ",(") + std::to_string(e.owner) + "," + std::to_string(e.item) + "," +
                   std::to_string(e.entry) + ",'" + e.kind + "','" + e.source + "'," + std::to_string(e.detail) +
                   "," + std::to_string(e.at) + ")";
        }
        CharacterDatabase.Execute(sql);
    }
}

void Flush()
{
    std::vector<Row> rows;
    std::vector<Event> evs;
    {
        std::lock_guard<std::mutex> guard(lock);
        rows.swap(queue);
        evs.swap(events);
    }
    if (!tableReady)
        return;
    FlushEvents(evs);
    if (rows.empty())
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
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `coa_item_event` ("
        "`id` INT UNSIGNED NOT NULL AUTO_INCREMENT, `owner` INT UNSIGNED NOT NULL, `item` INT UNSIGNED NOT NULL, "
        "`entry` INT UNSIGNED NOT NULL, `kind` VARCHAR(8) NOT NULL, `source` VARCHAR(16) NOT NULL DEFAULT '', "
        "`detail` INT UNSIGNED NOT NULL DEFAULT 0, `at` INT UNSIGNED NOT NULL, "
        "PRIMARY KEY (`id`), KEY `owner_at` (`owner`, `at`)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
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
    // The events of characters deleted.
    CharacterDatabase.Execute("DELETE e FROM `coa_item_event` e LEFT JOIN `characters` c ON c.`guid` = e.`owner` "
                              "WHERE c.`guid` IS NULL");
}

// Each character keeps its last EventsKept events (the newest first by id: the order they were written).
void TrimEvents()
{
    if (!tableReady)
        return;
    CharacterDatabase.Execute("DELETE e FROM `coa_item_event` e JOIN (SELECT `id`, ROW_NUMBER() OVER "
                              "(PARTITION BY `owner` ORDER BY `id` DESC) AS `n` FROM `coa_item_event`) r "
                              "ON r.`id` = e.`id` WHERE r.`n` > {}", EventsKept);
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
                                                     PLAYERHOOK_ON_AFTER_STORE_OR_EQUIP_NEW_ITEM,
                                                     PLAYERHOOK_ON_CREATE_ITEM, PLAYERHOOK_ON_SPELL_CAST })
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
            // Bought and worn at once: OnPlayerEquip came first, with no origin; its "equip" event gets the vendor.
            {
                std::lock_guard<std::mutex> guard(lock);
                uint32 const itemId = uint32(item->GetGUID().GetCounter());
                if (!events.empty() && events.back().item == itemId && !*events.back().source)
                {
                    events.back().source = "vendor";
                    events.back().detail = npc;
                }
                if (auto itr = worn.find(itemId); itr != worn.end())
                    itr->second.source = "vendor";
            }
            if (settings.enabled)
                Queue(player, item, "vendor", npc);
            return;
        }
        std::lock_guard<std::mutex> guard(lock);
        NoteLocked(item->GetGUID().GetCounter(), "vendor", npc);
    }

    // A piece made with a profession spell (a player's or a bot's cast): "craft", the spell, its maker.
    void OnPlayerCreateItem(Player* player, Item* item, uint32 /*count*/) override
    {
        if (!player || !item || !Wanted(item->GetTemplate()))
            return;
        Spell const* spell = player->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (!spell || !spell->GetSpellInfo())
            return;
        uint32 const spellId = spell->GetSpellInfo()->Id;
        bool profession = false;
        SkillLineAbilityMapBounds const bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
        for (auto itr = bounds.first; itr != bounds.second && !profession; ++itr)
            profession = IsProfessionSkill(itr->second->SkillLine);
        if (!profession)
            return;  // a quest's or a conjured item: not a craft, never a guess
        std::lock_guard<std::mutex> guard(lock);
        NoteLocked(item->GetGUID().GetCounter(), "craft", spellId, uint32(player->GetGUID().GetCounter()));
    }

    // An enchanting spell or scroll, a player's or a bot's: one "enchant" event on the target piece.
    void OnPlayerSpellCast(Player* /*player*/, Spell* spell, bool /*skipCheck*/) override
    {
        if (!settings.events || !spell || !spell->GetSpellInfo())
            return;
        SpellInfo const* info = spell->GetSpellInfo();
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            uint32 const effect = info->Effects[i].Effect;
            if (effect != SPELL_EFFECT_ENCHANT_ITEM && effect != SPELL_EFFECT_ENCHANT_ITEM_PRISMATIC)
                continue;
            Item* item = spell->m_targets.GetItemTarget();
            Player* owner = item ? item->GetOwner() : nullptr;
            if (!owner || !Wanted(item->GetTemplate()))
                return;
            EnchantmentSlot const slot =
                effect == SPELL_EFFECT_ENCHANT_ITEM ? PERM_ENCHANTMENT_SLOT : PRISMATIC_ENCHANTMENT_SLOT;
            uint32 const enchant = uint32(info->Effects[i].MiscValue);
            if (!NewEnchant(item->GetEnchantmentId(slot), enchant))
                return;
            std::lock_guard<std::mutex> guard(lock);
            QueueEventLocked(uint32(owner->GetGUID().GetCounter()), item, "enchant", "spell", enchant,
                             uint32(time(nullptr)));
            return;
        }
    }

    void OnPlayerEquip(Player* player, Item* item, uint8 /*bag*/, uint8 slot, bool /*update*/) override
    {
        if (!player || !item || slot >= EQUIPMENT_SLOT_END || !Wanted(item->GetTemplate()))
            return;
        uint32 const wearer = uint32(player->GetGUID().GetCounter());
        uint32 const itemId = uint32(item->GetGUID().GetCounter());
        uint32 const now = uint32(time(nullptr));
        // At login every worn piece passes here (QuickEquipItem, not in the world yet): no event, no maker row.
        bool const live = player->IsInWorld();
        Note note{ nullptr, 0, 0 };
        {
            std::lock_guard<std::mutex> guard(lock);
            auto itr = notes.find(itemId);
            if (itr != notes.end())
            {
                note = itr->second;
                notes.erase(itr);
            }
        }
        ObjectGuid const creator = item->GetGuidValue(ITEM_FIELD_CREATOR);
        uint32 const maker = note.maker ? note.maker : (creator.IsPlayer() ? uint32(creator.GetCounter()) : 0);
        // 3.3 bench: "start" was guessed for any new piece of unseen origin (698 dungeon pieces shown "Starting gear").
        char const* source = WornSource(note.source, startScopes > 0, maker != 0, maker == wearer);
        if (!note.source && !startScopes && !live)
            source = nullptr;

        bool firstSeen = false;
        {
            std::lock_guard<std::mutex> guard(lock);
            if (worn.size() >= MAX_WORN)
                worn.clear();
            auto [itr, added] = worn.try_emplace(itemId, Worn{ nullptr, 0 });
            firstSeen = added;
            char const* eventSource = source ? source : itr->second.source;  // worn again: its last pose's source
            if (settings.events && live && !RepeatPose(itr->second.at, now))
            {
                itr->second = { eventSource, now };
                QueueEventLocked(wearer, item, "equip", eventSource, note.detail, now);
            }
        }
        // coa_item_origin: an origin noted or a factory piece, as in 3.3; a made piece once per server start.
        if (settings.enabled && source && (note.source || startScopes || firstSeen))
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
            TrimEvents();
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

void CoaWorld::NoteCraft(Item* item, Player* maker, uint32 spell)
{
    if (!item || !maker || !Wanted(item->GetTemplate()))
        return;
    std::lock_guard<std::mutex> guard(lock);
    NoteLocked(item->GetGUID().GetCounter(), "craft", spell, uint32(maker->GetGUID().GetCounter()));
}

void CoaWorld::SetEnchant(Player* owner, Item* item, EnchantmentSlot slot, uint32 enchantId, ObjectGuid caster,
                          char const* source)
{
    if (!owner || !item)
        return;
    uint32 const before = item->GetEnchantmentId(slot);
    owner->ApplyEnchantment(item, slot, false);
    item->SetEnchantment(slot, enchantId, 0, 0, caster);
    owner->ApplyEnchantment(item, slot, true);
    if (!settings.events || !source || !NewEnchant(before, enchantId) || !Wanted(item->GetTemplate()))
        return;
    std::lock_guard<std::mutex> guard(lock);
    QueueEventLocked(uint32(owner->GetGUID().GetCounter()), item, "enchant", source, enchantId, uint32(time(nullptr)));
}

CoaWorld::StartGearScope::StartGearScope() { ++startScopes; }
CoaWorld::StartGearScope::~StartGearScope() { --startScopes; }

void AddSC_coa_world_item_origin()
{
    new CoaWorldItemOriginPlayer();
    new CoaWorldItemOriginAuction();
    new CoaWorldItemOriginWorld();
}
