/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

// Random bot transmog (AiPlayerbot.RandomBotTransmog). A share of the random bots show another model of the same
// armor or weapon type on each visible slot, as players do with their wardrobe. Only the visible item id sent to
// other clients changes: the real items, their stats and enchantments stay as they are, and nothing is saved. The
// model is drawn from the bot's guid and level bracket, so a bot looks the same at every login and its outfit moves
// on as it levels. The catalog holds the source items of Appearances.dbc found in item_template, read once at start.
// The armor slots show one whole item set (all eight pieces) or none: an armor set of the bot's armor type within its
// level window, or a cosmetic set (artifact quality, sold for their look, as Tiraxis' sets) of any armor type once
// the bot has the set's level. Without a whole set, each slot shows a model of its own as before.

#include "ClientDBC.h"
#include "Config.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "Timer.h"
#include "WorldSession.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    constexpr uint32 LevelBracket = 5;
    constexpr uint32 LevelWindow = 15;
    constexpr uint32 SetSlotCount = 8;

    struct CatalogItem
    {
        uint32 itemId;
        uint32 level;
        uint32 classMask;
        uint32 raceMask;
    };

    struct CatalogSet
    {
        uint32 setId;
        uint32 maxLevel;
        uint32 classMask;
        uint32 raceMask;
        std::vector<std::pair<uint32, uint32>> pieces; // inventory type, item id
    };

    struct Catalog
    {
        // Key: item class, subclass and inventory type. Items sorted by level.
        std::unordered_map<uint32, std::vector<CatalogItem>> buckets;
        // Whole armor sets by armor subclass (cloth 1 to plate 4), sorted by set id.
        std::vector<CatalogSet> sets[ITEM_SUBCLASS_ARMOR_PLATE + 1];
        // Whole cosmetic sets (artifact quality), for any armor type, sorted by set id.
        std::vector<CatalogSet> cosmeticSets;
        // Characters with active appearances of their own: the core shows those.
        std::unordered_set<uint32> ownAppearances;
        bool collectionsForBots = false;
        bool loaded = false;
    };

    Catalog TransmogCatalog;

    uint32 NormalizedInventoryType(uint32 inventoryType)
    {
        return inventoryType == INVTYPE_ROBE ? uint32(INVTYPE_CHEST) : inventoryType;
    }

    uint32 BucketKey(uint32 itemClass, uint32 subClass, uint32 inventoryType)
    {
        return (itemClass << 16) | ((subClass & 0xFF) << 8) | (NormalizedInventoryType(inventoryType) & 0xFF);
    }

    bool IsSetSlot(uint32 inventoryType)
    {
        switch (NormalizedInventoryType(inventoryType))
        {
            case INVTYPE_HEAD:
            case INVTYPE_SHOULDERS:
            case INVTYPE_CHEST:
            case INVTYPE_WAIST:
            case INVTYPE_LEGS:
            case INVTYPE_FEET:
            case INVTYPE_WRISTS:
            case INVTYPE_HANDS:
                return true;
            default:
                return false;
        }
    }

    bool IsTransmogSlot(uint8 slot)
    {
        switch (slot)
        {
            case EQUIPMENT_SLOT_HEAD:
            case EQUIPMENT_SLOT_SHOULDERS:
            case EQUIPMENT_SLOT_BACK:
            case EQUIPMENT_SLOT_CHEST:
            case EQUIPMENT_SLOT_WRISTS:
            case EQUIPMENT_SLOT_HANDS:
            case EQUIPMENT_SLOT_WAIST:
            case EQUIPMENT_SLOT_LEGS:
            case EQUIPMENT_SLOT_FEET:
            case EQUIPMENT_SLOT_MAINHAND:
            case EQUIPMENT_SLOT_OFFHAND:
            case EQUIPMENT_SLOT_RANGED:
                return true;
            default:
                return false;
        }
    }

    bool HasWord(std::string_view name, std::string_view word)
    {
        for (size_t pos = name.find(word); pos != std::string_view::npos; pos = name.find(word, pos + 1))
        {
            bool const startOk = pos == 0 || !std::isalnum(static_cast<unsigned char>(name[pos - 1]));
            size_t const end = pos + word.size();
            bool const endOk = end >= name.size() || !std::isalnum(static_cast<unsigned char>(name[end]));
            if (startOk && endOk)
                return true;
        }
        return false;
    }

    bool IsPlaceholderName(std::string_view name)
    {
        if (name.empty() || name.find("[PH]") != std::string_view::npos || name.find("Monster -") != std::string_view::npos ||
            name.find("Deprecated") != std::string_view::npos || name.find("DEPRECATED") != std::string_view::npos ||
            name.starts_with("zz") || name.starts_with("ZZ"))
            return true;

        for (std::string_view word : {"Test", "TEST", "test", "OLD", "Unused", "UNUSED", "DEP", "QA"})
            if (HasWord(name, word))
                return true;

        return false;
    }

    uint64 Mix(uint64 value)
    {
        value += 0x9E3779B97F4A7C15ULL;
        value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
        value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
        return value ^ (value >> 31);
    }

    uint64 Roll(uint32 guid, uint32 bracket, uint32 salt)
    {
        return Mix((uint64(guid) << 32) ^ Mix((uint64(bracket) << 16) ^ salt));
    }

    void LoadCatalog()
    {
        uint32 const oldMSTime = getMSTime();
        Catalog catalog;

        catalog.collectionsForBots = sConfigMgr->GetOption<uint32>("CoA.CollectionsForBots", 1, false) >= 2;
        if (!sPlayerbotAIConfig.randomBotTransmog || !sPlayerbotAIConfig.randomBotTransmogPercent)
        {
            TransmogCatalog = std::move(catalog);
            return;
        }

        if (catalog.collectionsForBots)
        {
            LOG_INFO("server.loading", "Random bot transmog off: CoA.CollectionsForBots >= 2 shows the bots' own appearances.");
            TransmogCatalog = std::move(catalog);
            return;
        }

        // Source items of the equipment appearances (categories 1 to 14): the items a client can display.
        std::unordered_set<uint32> sourceItems;
        ClientDBC appearances;
        if (appearances.Load(GetClientDBCPath("Appearances.dbc"), 8))
        {
            for (uint32 row = 0; row < appearances.GetRecordCount(); ++row)
            {
                ClientDBC::Record const record = appearances.GetRecord(row);
                uint32 const sourceItem = record.GetUInt32(3);
                if (!record.GetUInt32(0) || !sourceItem)
                    continue;

                for (uint32 field : {5u, 6u, 7u})
                {
                    uint32 const category = record.GetUInt32(field);
                    if (category >= 1 && category <= 14)
                    {
                        sourceItems.insert(sourceItem);
                        break;
                    }
                }
            }
        }

        if (sourceItems.empty())
        {
            LOG_WARN("server.loading", "Random bot transmog off: no equipment appearance read from Appearances.dbc.");
            TransmogCatalog = std::move(catalog);
            return;
        }

        std::map<std::pair<uint32, uint32>, CatalogSet> setsById; // (armor subclass, set id); 0: cosmetic
        uint32 itemCount = 0;
        for (auto const& [itemId, proto] : *sObjectMgr->GetItemTemplateStore())
        {
            if (!sourceItems.contains(itemId))
                continue;
            if (proto.Class != ITEM_CLASS_ARMOR && proto.Class != ITEM_CLASS_WEAPON)
                continue;
            bool const cosmetic = proto.Quality == ITEM_QUALITY_ARTIFACT;
            if ((proto.Quality < ITEM_QUALITY_UNCOMMON || proto.Quality > ITEM_QUALITY_EPIC) && !cosmetic)
                continue;
            if (proto.HasFlag(ITEM_FLAG_DEPRECATED) || IsPlaceholderName(proto.Name1))
                continue;
            if (proto.InventoryType == INVTYPE_NON_EQUIP || proto.InventoryType == INVTYPE_BODY ||
                proto.InventoryType == INVTYPE_TABARD)
                continue;
            if (!proto.AllowableClass || !proto.AllowableRace)
                continue;

            bool const setPiece = proto.ItemSet && proto.Class == ITEM_CLASS_ARMOR &&
                                  proto.SubClass >= ITEM_SUBCLASS_ARMOR_CLOTH &&
                                  proto.SubClass <= ITEM_SUBCLASS_ARMOR_PLATE && IsSetSlot(proto.InventoryType);
            if (cosmetic && !setPiece)
                continue;

            uint32 const level = proto.RequiredLevel ? proto.RequiredLevel : proto.ItemLevel;
            if (!cosmetic)
            {
                catalog.buckets[BucketKey(proto.Class, proto.SubClass, proto.InventoryType)].push_back(
                    CatalogItem{itemId, level, proto.AllowableClass, proto.AllowableRace});
                ++itemCount;
            }

            if (setPiece)
            {
                auto [itr, inserted] = setsById.try_emplace(std::make_pair(cosmetic ? 0u : proto.SubClass, proto.ItemSet));
                CatalogSet& set = itr->second;
                if (inserted)
                {
                    set.setId = proto.ItemSet;
                    set.maxLevel = 0;
                    set.classMask = proto.AllowableClass;
                    set.raceMask = proto.AllowableRace;
                }
                uint32 const inventoryType = NormalizedInventoryType(proto.InventoryType);
                bool const known = std::any_of(set.pieces.begin(), set.pieces.end(),
                                               [inventoryType](auto const& piece) { return piece.first == inventoryType; });
                if (!known)
                {
                    set.pieces.emplace_back(inventoryType, itemId);
                    set.maxLevel = std::max(set.maxLevel, level);
                    set.classMask &= proto.AllowableClass;
                    set.raceMask &= proto.AllowableRace;
                }
            }
        }

        for (auto& [key, items] : catalog.buckets)
            std::sort(items.begin(), items.end(), [](CatalogItem const& a, CatalogItem const& b)
                      { return a.level != b.level ? a.level < b.level : a.itemId < b.itemId; });

        uint32 setCount = 0;
        uint32 cosmeticCount = 0;
        for (auto& [key, set] : setsById)
        {
            if (set.pieces.size() < SetSlotCount || !set.classMask || !set.raceMask)
                continue;
            if (key.first)
            {
                catalog.sets[key.first].push_back(std::move(set));
                ++setCount;
            }
            else
            {
                catalog.cosmeticSets.push_back(std::move(set));
                ++cosmeticCount;
            }
        }

        if (QueryResult result = CharacterDatabase.Query("SELECT DISTINCT `guid` FROM `character_appearance`"))
        {
            do
            {
                catalog.ownAppearances.insert(result->Fetch()[0].Get<uint32>());
            } while (result->NextRow());
        }

        catalog.loaded = itemCount > 0;
        TransmogCatalog = std::move(catalog);
        LOG_INFO("server.loading",
                 ">> Random bot transmog: {} appearances, {} whole item sets and {} cosmetic sets for {}% of the random "
                 "bots in {} ms",
                 itemCount, setCount, cosmeticCount, sPlayerbotAIConfig.randomBotTransmogPercent,
                 GetMSTimeDiffToNow(oldMSTime));
    }

    bool Fits(uint32 itemClassMask, uint32 itemRaceMask, Player const* bot)
    {
        return (itemClassMask & bot->getClassMask()) && (itemRaceMask & bot->getRaceMask());
    }

    uint32 ArmorSubClass(Player const* bot)
    {
        if (bot->HasSkill(SKILL_PLATE_MAIL))
            return ITEM_SUBCLASS_ARMOR_PLATE;
        if (bot->HasSkill(SKILL_MAIL))
            return ITEM_SUBCLASS_ARMOR_MAIL;
        if (bot->HasSkill(SKILL_LEATHER))
            return ITEM_SUBCLASS_ARMOR_LEATHER;
        return ITEM_SUBCLASS_ARMOR_CLOTH;
    }

    template <typename Allowed>
    CatalogSet const* PickFrom(std::vector<CatalogSet> const& sets, Player const* bot, uint32 bracket, uint32 salt,
                               Allowed allowed)
    {
        uint32 count = 0;
        for (CatalogSet const& set : sets)
            if (allowed(set) && Fits(set.classMask, set.raceMask, bot))
                ++count;
        if (!count)
            return nullptr;

        uint32 pick = uint32(Roll(bot->GetGUID().GetCounter(), bracket, salt) % count);
        for (CatalogSet const& set : sets)
            if (allowed(set) && Fits(set.classMask, set.raceMask, bot) && !pick--)
                return &set;
        return nullptr;
    }

    // One set for every armor slot of the bot in this level bracket, or none.
    CatalogSet const* PickOutfit(Player const* bot, uint32 level, uint32 low, uint32 high, uint32 bracket)
    {
        uint32 const guid = bot->GetGUID().GetCounter();
        if (Roll(guid, bracket, 0xC05E) % 100 < sPlayerbotAIConfig.randomBotTransmogCosmeticSets)
            if (CatalogSet const* set = PickFrom(TransmogCatalog.cosmeticSets, bot, bracket, 0xC05E7,
                                                 [level](CatalogSet const& set) { return set.maxLevel <= level; }))
                return set;

        uint32 const subClass = ArmorSubClass(bot);
        return PickFrom(TransmogCatalog.sets[subClass], bot, bracket, 0x5E70 + subClass,
                        [low, high](CatalogSet const& set) { return set.maxLevel >= low && set.maxLevel <= high; });
    }

    uint32 PickSetPiece(Player const* bot, ItemTemplate const* real, uint32 level, uint32 low, uint32 high,
                        uint32 bracket)
    {
        if (real->Class != ITEM_CLASS_ARMOR || !IsSetSlot(real->InventoryType))
            return 0;

        CatalogSet const* set = PickOutfit(bot, level, low, high, bracket);
        if (!set)
            return 0;

        uint32 const wanted = NormalizedInventoryType(real->InventoryType);
        for (auto const& [pieceType, itemId] : set->pieces)
            if (pieceType == wanted)
                return itemId;
        return 0;
    }

    uint32 PickItem(Player const* bot, uint8 slot, ItemTemplate const* real, uint32 low, uint32 high, uint32 bracket)
    {
        auto bucket = TransmogCatalog.buckets.find(BucketKey(real->Class, real->SubClass, real->InventoryType));
        if (bucket == TransmogCatalog.buckets.end())
            return 0;

        std::vector<CatalogItem> const& items = bucket->second;
        auto first = std::lower_bound(items.begin(), items.end(), low,
                                      [](CatalogItem const& item, uint32 level) { return item.level < level; });
        auto last = std::upper_bound(first, items.end(), high,
                                     [](uint32 level, CatalogItem const& item) { return level < item.level; });

        uint32 count = 0;
        for (auto itr = first; itr != last; ++itr)
            if (Fits(itr->classMask, itr->raceMask, bot))
                ++count;
        if (!count)
            return 0;

        uint32 pick = uint32(Roll(bot->GetGUID().GetCounter(), bracket, slot) % count);
        for (auto itr = first; itr != last; ++itr)
            if (Fits(itr->classMask, itr->raceMask, bot) && !pick--)
                return itr->itemId;
        return 0;
    }

    bool UsesTransmog(Player* bot, uint32 level)
    {
        if (!TransmogCatalog.loaded || TransmogCatalog.collectionsForBots || !sPlayerbotAIConfig.randomBotTransmog)
            return false;
        if (level < sPlayerbotAIConfig.randomBotTransmogMinLevel || PlayerbotFactory::SelfFoundAt(level) ||
            PlayerbotFactory::IsSelfFoundBot(bot))
            return false;

        WorldSession* session = bot->GetSession();
        if (!session || !session->IsBot())
            return false;

        uint32 const guid = bot->GetGUID().GetCounter();
        if (Mix(uint64(guid) ^ 0x7A5000000000ULL) % 100 >= sPlayerbotAIConfig.randomBotTransmogPercent)
            return false;
        if (TransmogCatalog.ownAppearances.contains(guid))
            return false;

        return sRandomPlayerbotMgr.IsRandomBot(bot);
    }
}

class RandomBotTransmogPlayerScript : public PlayerScript
{
public:
    RandomBotTransmogPlayerScript()
        : PlayerScript("RandomBotTransmogPlayerScript", {PLAYERHOOK_ON_AFTER_SET_VISIBLE_ITEM_SLOT})
    {
    }

    void OnPlayerAfterSetVisibleItemSlot(Player* player, uint8 slot, Item* item) override
    {
        if (!player || !item || !IsTransmogSlot(slot))
            return;

        ItemTemplate const* real = item->GetTemplate();
        if (!real || (real->Class != ITEM_CLASS_ARMOR && real->Class != ITEM_CLASS_WEAPON))
            return;

        uint32 const level = player->GetLevel();
        if (!UsesTransmog(player, level))
            return;

        uint32 const bracket = level / LevelBracket;
        uint32 const high = std::max(bracket * LevelBracket, 1u);
        uint32 const low = high > LevelWindow ? high - LevelWindow : 0;

        uint32 shown = PickSetPiece(player, real, level, low, high, bracket);
        if (!shown)
            shown = PickItem(player, slot, real, low, high, bracket);

        if (shown && shown != item->GetEntry())
            player->SetUInt32Value(PLAYER_VISIBLE_ITEM_1_ENTRYID + (slot * 2), shown);
    }
};

class RandomBotTransmogWorldScript : public WorldScript
{
public:
    RandomBotTransmogWorldScript() : WorldScript("RandomBotTransmogWorldScript", {WORLDHOOK_ON_STARTUP}) {}

    void OnStartup() override { LoadCatalog(); }
};

void AddSC_random_bot_transmog()
{
    new RandomBotTransmogPlayerScript();
    new RandomBotTransmogWorldScript();
}
