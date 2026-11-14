/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaCompanionLootCompat.h"

#ifndef COA_HAS_CORE_COMPANION_LOOT

#include "Creature.h"
#include "DatabaseEnv.h"
#include "Map.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"
#include "WorldSession.h"

#include <algorithm>
#include <array>
#include <mutex>
#include <unordered_map>

namespace
{
// The core keeps the active appearances in memory and writes them to the database on every change; the module reads
// them at most once per CacheMs per character (never once per tick or per corpse).
constexpr uint32 CacheMs = 30000;
// Goblin Salvage Field Testing: while one of these quests is in the log, the core loots only within 10 yards.
constexpr std::array<uint32, 2> FieldTestingQuests = {176003, 176013};
constexpr float FieldTestingLootReach = 10.0f;

struct Appearances
{
    uint32 readAt = 0;
    bool loot = false;
    bool skin = false;
};

std::mutex cacheLock;
std::unordered_map<ObjectGuid::LowType, Appearances> cache;

Appearances Read(Player const* player)
{
    ObjectGuid::LowType const guid = player->GetGUID().GetCounter();
    {
        std::lock_guard<std::mutex> guard(cacheLock);
        auto const itr = cache.find(guid);
        if (itr != cache.end() && getMSTimeDiff(itr->second.readAt, getMSTime()) < CacheMs)
            return itr->second;
    }

    bool activeLoot = false;
    bool activeSkin = false;
    if (QueryResult result = CharacterDatabase.Query(
            "SELECT `category_id`, `appearance_id` FROM `character_appearance` WHERE `guid` = {} AND `category_id` IN ({}, {})",
            guid, CoaCompanionLoot::LootAppearanceCategory, CoaCompanionLoot::SkinningAppearanceCategory))
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 const category = fields[0].Get<uint32>();
            uint32 const appearance = fields[1].Get<uint32>();
            activeLoot |= category == CoaCompanionLoot::LootAppearanceCategory &&
                          appearance == CoaCompanionLoot::LootTransfiguratorAppearance;
            activeSkin |= category == CoaCompanionLoot::SkinningAppearanceCategory &&
                          appearance == CoaCompanionLoot::SkinPeelerAppearance;
        } while (result->NextRow());
    }

    Appearances read;
    read.readAt = getMSTime();
    // As in the core: active AND collected by the account.
    if ((activeLoot || activeSkin) && player->GetSession())
        if (QueryResult result = CharacterDatabase.Query(
                "SELECT `appearance_id` FROM `account_appearance_collection` WHERE `account_id` = {} AND `appearance_id` IN ({}, {})",
                player->GetSession()->GetAccountId(), CoaCompanionLoot::LootTransfiguratorAppearance,
                CoaCompanionLoot::SkinPeelerAppearance))
        {
            do
            {
                uint32 const appearance = result->Fetch()[0].Get<uint32>();
                read.loot |= activeLoot && appearance == CoaCompanionLoot::LootTransfiguratorAppearance;
                read.skin |= activeSkin && appearance == CoaCompanionLoot::SkinPeelerAppearance;
            } while (result->NextRow());
        }

    std::lock_guard<std::mutex> guard(cacheLock);
    cache[guid] = read;
    return read;
}

bool FieldTesting(Player const* player)
{
    return std::any_of(FieldTestingQuests.begin(), FieldTestingQuests.end(),
                       [player](uint32 questId) { return player->FindQuestSlot(questId) < MAX_QUEST_LOG_SIZE; });
}

float Radius(Player const* player, bool skin)
{
    SpellInfo const* spell = sSpellMgr->GetSpellInfo(skin ? CoaCompanionLoot::SkinPeelerSpell
                                                          : CoaCompanionLoot::LootTransfiguratorSpell);
    if (!spell || !spell->Effects[EFFECT_0].Amplitude)
        return 0.0f;
    float const radius = spell->Effects[EFFECT_0].CalcRadius(const_cast<Player*>(player));
    if (!skin && FieldTesting(player))
        return std::min(radius, FieldTestingLootReach);
    return radius;
}

bool Loots(Player const* player, bool skin)
{
    Map* map = player->FindMap();
    if (!map || !player->GetCritterGUID() || Radius(player, skin) <= 0.0f)
        return false;
    Creature* companion = map->GetCreature(player->GetCritterGUID());
    if (!companion || !companion->IsAlive() || companion->GetOwnerGUID() != player->GetGUID())
        return false;
    Appearances const appearances = Read(player);
    if (skin)
        return CoaCompanionLoot::SkinsWith(appearances.skin);
    return CoaCompanionLoot::LootsWith(appearances.loot, CoaCompanionLoot::IsSelfLootingCompanion(companion->GetEntry()),
                                       map->IsRaid());
}
}  // namespace

bool CoaCompanionLoot::HasLootAppearance(Player const* player) { return player && Read(player).loot; }

bool CoaCompanionLoot::HasSkinningAppearance(Player const* player) { return player && Read(player).skin; }

bool CoaCompanionLoot::CompanionLoots(Player const* player) { return player && Loots(player, false); }

bool CoaCompanionLoot::CompanionSkins(Player const* player) { return player && Loots(player, true); }

float CoaCompanionLoot::LootRadius(Player const* player) { return player ? Radius(player, false) : 0.0f; }

float CoaCompanionLoot::SkinningRadius(Player const* player) { return player ? Radius(player, true) : 0.0f; }

#endif
