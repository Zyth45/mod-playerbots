/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaMissionMgr.h"

#include "Bag.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "Engine.h"
#include "GameObject.h"
#include "Group.h"
#include "Item.h"
#include "Mail.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotMgr.h"
#include "PlayerbotOperations.h"
#include "PlayerbotWorldThreadProcessor.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "Timer.h"
#include "TravelMgr.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace
{
// Keys of playerbots_random_bots (owner 0, the alt's guid), 255 characters of data each.
constexpr char const* MISSION_KEY = "coa_mission";
constexpr char const* MISSION_ITEMS_KEY = "coa_mission_items";
constexpr char const* MISSION_SAVE_VERSION = "2";

std::vector<std::string> SplitFields(std::string const& text, char separator)
{
    std::vector<std::string> fields;
    std::string field;
    std::istringstream in(text);
    while (std::getline(in, field, separator))
        fields.push_back(field);
    return fields;
}

char const* StateName(CoaMissionState state)
{
    switch (state)
    {
        case CoaMissionState::Travel:
            return "TRAVEL";
        case CoaMissionState::Work:
            return "WORK";
        case CoaMissionState::Deliver:
            return "DELIVER";
    }
    return "NONE";
}

std::string ZoneName(uint32 zoneId)
{
    AreaTableEntry const* zone = sAreaTableStore.LookupEntry(zoneId);
    std::string name = zone ? PlayerbotAI::GetLocalizedAreaName(zone) : "";
    // The status line uses ';' between its fields.
    std::replace(name.begin(), name.end(), ';', ',');
    return name.empty() ? "?" : name;
}

// Does this node need Herbalism or Mining? Only what the mission gathers from those is mailed.
bool IsGatheringNode(GameObject* go)
{
    if (!go)
        return false;
    LockEntry const* lock = sLockStore.LookupEntry(go->GetGOInfo()->GetLockId());
    if (!lock)
        return false;
    for (uint8 i = 0; i < MAX_LOCK_CASE; ++i)
    {
        if (lock->Type[i] != LOCK_KEY_SKILL)
            continue;
        SkillType const skill = SkillByLockType(LockType(lock->Index[i]));
        if (skill == SKILL_HERBALISM || skill == SKILL_MINING)
            return true;
    }
    return false;
}
}  // namespace

bool CoaMissionMgr::IsOnMission(Player* bot) const
{
    if (!bot || !active.load())
        return false;
    std::lock_guard<std::mutex> guard(lock);
    return missions.find(bot->GetGUID().GetCounter()) != missions.end();
}

bool CoaMissionMgr::Get(Player* bot, CoaMission& out) const
{
    if (!bot || !active.load())
        return false;
    std::lock_guard<std::mutex> guard(lock);
    auto const it = missions.find(bot->GetGUID().GetCounter());
    if (it == missions.end())
        return false;
    out = it->second;
    return true;
}

void CoaMissionMgr::Put(Player* bot, CoaMission const& mission)
{
    if (!bot)
        return;
    std::lock_guard<std::mutex> guard(lock);
    auto const it = missions.find(bot->GetGUID().GetCounter());
    if (it == missions.end())
        return;
    std::map<uint32, uint32> items = std::move(it->second.items);
    uint32 const lastLootMs = it->second.lastLootMs;
    it->second = mission;
    it->second.items = std::move(items);
    it->second.lastLootMs = lastLootMs;
}

uint32 CoaMissionMgr::CountForOwner(ObjectGuid::LowType owner) const
{
    uint32 count = 0;
    for (auto const& [bot, mission] : missions)
        if (mission.owner == owner)
            ++count;
    return count;
}

bool CoaMissionMgr::Begin(Player* bot, CoaMission const& mission, std::string& error)
{
    std::lock_guard<std::mutex> guard(lock);
    if (missions.find(bot->GetGUID().GetCounter()) != missions.end())
    {
        error = "I am already on a mission";
        return false;
    }
    if (!mission.bench && sPlayerbotAIConfig.missionsMaxPerPlayer &&
        CountForOwner(mission.owner) >= sPlayerbotAIConfig.missionsMaxPerPlayer)
    {
        error = "you already have " + std::to_string(sPlayerbotAIConfig.missionsMaxPerPlayer) + " characters on a mission";
        return false;
    }
    CoaMission& added = missions[bot->GetGUID().GetCounter()];
    added = mission;
    added.lootClockMs = getMSTime();
    active.store(uint32(missions.size()));
    return true;
}

void CoaMissionMgr::Remove(Player* bot)
{
    std::lock_guard<std::mutex> guard(lock);
    missions.erase(bot->GetGUID().GetCounter());
    active.store(uint32(missions.size()));
}

bool CoaMissionMgr::IsMissionBot(Player* bot, Player* requester)
{
    if (!bot || !requester || bot == requester)
        return false;
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI || !IsRealPlayer(requester) || botAI->GetMaster() != requester)
        return false;
    // Never a random bot, nor a bot of the random bot accounts (addclass).
    if (sRandomPlayerbotMgr.IsRandomBot(bot) || sPlayerbotAIConfig.IsInRandomAccountList(bot->GetSession()->GetAccountId()))
        return false;
    // The requester's own session holds this bot: it is one of the requester's characters.
    PlayerbotMgr* holder = GET_PLAYERBOT_MGR(requester);
    return holder && holder->GetPlayerBot(bot->GetGUID()) == bot;
}

std::string CoaMissionMgr::SkillName(uint32 skillId)
{
    return skillId == SKILL_MINING ? "mining" : skillId == SKILL_HERBALISM ? "herbalism" : "?";
}

bool CoaMissionMgr::IsBench(Player* bot) const
{
    if (!active.load() || !bot)
        return false;
    std::lock_guard<std::mutex> guard(lock);
    auto const it = missions.find(bot->GetGUID().GetCounter());
    return it != missions.end() && it->second.bench;
}

void CoaMissionMgr::ApplyEngineStrategies(Engine* nonCombatEngine, bool bench)
{
    if (!nonCombatEngine)
        return;
    // A random bot of the bench: none of its own wandering while it gathers.
    if (bench)
        for (char const* name : {"grind", "rpg", "new rpg", "move random", "start duel", "bg", "quest"})
            nonCombatEngine->removeStrategy(name, false);
    nonCombatEngine->removeStrategy("follow", false);
    nonCombatEngine->removeStrategy("stay", false);
    nonCombatEngine->removeStrategy("lfg", false);
    nonCombatEngine->addStrategy("loot", false);
    nonCombatEngine->addStrategy("gather", false);
    nonCombatEngine->addStrategy("gather all", false);
    nonCombatEngine->addStrategy("mission", false);
}

uint32 CoaMissionMgr::ReadStrategyFlags(PlayerbotAI* botAI)
{
    uint32 flags = 0;
    if (botAI->HasStrategy("follow", BOT_STATE_NON_COMBAT))
        flags |= FLAG_FOLLOW;
    if (botAI->HasStrategy("stay", BOT_STATE_NON_COMBAT))
        flags |= FLAG_STAY;
    if (botAI->HasStrategy("lfg", BOT_STATE_NON_COMBAT))
        flags |= FLAG_LFG;
    if (botAI->HasStrategy("loot", BOT_STATE_NON_COMBAT))
        flags |= FLAG_LOOT;
    if (botAI->HasStrategy("gather", BOT_STATE_NON_COMBAT))
        flags |= FLAG_GATHER;
    if (botAI->HasStrategy("gather all", BOT_STATE_NON_COMBAT))
        flags |= FLAG_GATHER_ALL;
    return flags;
}

std::string CoaMissionMgr::StatusLine(Player* bot) const
{
    CoaMission mission;
    if (!Get(bot, mission))
        return "MISSION;NONE";

    uint32 items = 0;
    for (auto const& [entry, count] : mission.items)
        items += count;
    items += mission.mailed;
    int32 const minutes = mission.remainingMs > 0 ? (mission.remainingMs + MINUTE * IN_MILLISECONDS - 1) / (MINUTE * IN_MILLISECONDS) : 0;

    std::ostringstream out;
    out << "MISSION;" << StateName(mission.state) << ';' << ZoneName(mission.zoneId) << ';' << minutes << ';' << items
        << ';' << SkillName(mission.skillId) << ';' << mission.deaths << ';' << (mission.risk ? 1 : 0);
    return out.str();
}

void CoaMissionMgr::Save(Player* bot)
{
    CoaMission mission;
    if (!Get(bot, mission) || mission.bench)
        return;

    std::ostringstream out;
    out << MISSION_SAVE_VERSION << ';' << mission.owner << ';' << mission.skillId << ';' << uint32(mission.state) << ';'
        << std::max<int32>(0, mission.remainingMs / IN_MILLISECONDS) << ';' << mission.totalMinutes << ';' << mission.mapId
        << ';' << std::fixed << std::setprecision(2) << mission.x << ';' << mission.y << ';' << mission.z << ';'
        << mission.zoneId << ';' << mission.point << ';' << mission.deaths << ';' << mission.fails << ';'
        << mission.prevFlags << ';' << mission.orderItem << ';' << mission.orderCount << ';' << (mission.risk ? 1 : 0)
        << ';' << mission.mailed;
    sRandomPlayerbotMgr.SetValue(bot, MISSION_KEY, 1, out.str());

    std::string items;
    for (auto const& [entry, count] : mission.items)
    {
        std::string const part = std::to_string(entry) + ':' + std::to_string(count);
        if (items.size() + part.size() + 1 > 250)
            break;
        if (!items.empty())
            items += ',';
        items += part;
    }
    sRandomPlayerbotMgr.SetValue(bot, MISSION_ITEMS_KEY, items.empty() ? 0 : 1, items);
}

void CoaMissionMgr::ClearSaved(Player* bot)
{
    sRandomPlayerbotMgr.SetValue(bot, MISSION_KEY, 0);
    sRandomPlayerbotMgr.SetValue(bot, MISSION_ITEMS_KEY, 0);
}

// Mails to the owner what the mission gathered, never more than the bot still holds: whole stacks are moved as they
// are, a stack holding more is split. The bot's inventory is saved in the same transaction as the mails.
uint32 CoaMissionMgr::Deliver(Player* bot, ObjectGuid::LowType owner, std::map<uint32, uint32> const& items)
{
    if (!bot || !owner || items.empty() || owner == bot->GetGUID().GetCounter())
        return 0;

    ObjectGuid const ownerGuid = ObjectGuid::Create<HighGuid::Player>(owner);
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    std::vector<Item*> toMail;
    uint32 sent = 0;

    for (auto const& [entry, gathered] : items)
    {
        uint32 left = std::min<uint32>(gathered, bot->GetItemCount(entry, false));
        if (!left)
            continue;

        std::vector<Item*> stacks;
        auto consider = [&](Item* item)
        {
            if (item && item->GetEntry() == entry && !item->IsSoulBound() && !item->IsBoundAccountWide() &&
                !item->IsNotEmptyBag())
                stacks.push_back(item);
        };
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            consider(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
        for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
            if (Bag* bag = bot->GetBagByPos(bagSlot))
                for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                    consider(bag->GetItemByPos(uint8(slot)));

        for (Item* item : stacks)
        {
            if (!left)
                break;
            uint32 const count = item->GetCount();
            if (count <= left)
            {
                bot->MoveItemFromInventory(item->GetBagSlot(), item->GetSlot(), true);
                item->DeleteFromInventoryDB(trans);
                item->SetOwnerGUID(ownerGuid);
                item->SaveToDB(trans);
                toMail.push_back(item);
                left -= count;
                sent += count;
            }
            else
            {
                Item* part = Item::CreateItem(entry, left, bot);
                if (!part)
                    break;
                item->SetCount(count - left);
                item->SetState(ITEM_CHANGED, bot);
                part->SetOwnerGUID(ownerGuid);
                part->SaveToDB(trans);
                toMail.push_back(part);
                sent += left;
                left = 0;
            }
        }
    }

    if (toMail.empty())
    {
        CharacterDatabase.CommitTransaction(trans);
        return 0;
    }

    Player* receiver = ObjectAccessor::FindConnectedPlayer(ownerGuid);
    std::ostringstream body;
    body << "Here is what I gathered on my mission.";
    for (size_t first = 0; first < toMail.size(); first += MAX_MAIL_ITEMS)
    {
        MailDraft draft("Mission: what I gathered", body.str());
        for (size_t i = first; i < toMail.size() && i < first + MAX_MAIL_ITEMS; ++i)
            draft.AddItem(toMail[i]);
        draft.SendMailTo(trans, MailReceiver(receiver, owner), MailSender(bot));
    }
    bot->SaveInventoryAndGoldToDB(trans);
    CharacterDatabase.CommitTransaction(trans);
    return sent;
}

void CoaMissionMgr::RestoreStrategiesAndGroup(Player* bot, uint32 prevFlags)
{
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return;

    Player* master = botAI->GetMaster();
    bool const masterNear = master && master->IsInWorld() && master->GetMapId() == bot->GetMapId() &&
                            bot->GetExactDist(master) < 300.0f;

    // Back to the owner's group, as at login; never into a dungeon finder or battleground group.
    bool summoned = false;
    if (master && master->IsInWorld() && !bot->GetGroup())
    {
        Group* group = master->GetGroup();
        if (!group || (!group->isLFGGroup() && !group->isBGGroup() && !group->isBFGroup()))
        {
            PlayerbotWorldThreadProcessor::instance().QueueOperation(
                std::make_unique<GroupInviteOperation>(master->GetGUID(), bot->GetGUID()));
            summoned = !masterNear && sPlayerbotAIConfig.summonWhenGroup && bot->IsAlive();
        }
    }

    std::string change = "-mission";
    auto put = [&](uint32 flag, char const* name)
    {
        change += (prevFlags & flag) ? ",+" : ",-";
        change += name;
    };
    put(FLAG_LOOT, "loot");
    put(FLAG_GATHER, "gather");
    put(FLAG_GATHER_ALL, "gather all");
    put(FLAG_LFG, "lfg");

    // A bot far from its owner waits where it is: it is summoned, or told to follow.
    bool const follow = (prevFlags & FLAG_FOLLOW) && (masterNear || summoned);
    bool const stay = (prevFlags & FLAG_STAY) || ((prevFlags & FLAG_FOLLOW) && !follow);
    change += follow ? ",+follow" : ",-follow";
    change += stay ? ",+stay" : ",-stay";
    botAI->ChangeStrategy(change, BOT_STATE_NON_COMBAT);

    if ((prevFlags & FLAG_FOLLOW) && !follow)
        botAI->TellMasterNoFacing("I wait for you in " + ZoneName(bot->GetZoneId()) +
                                  ": summon me, or whisper \"follow\".");
}

void CoaMissionMgr::Finish(Player* bot, std::string const& reason)
{
    CoaMission mission;
    if (!Get(bot, mission))
        return;

    Remove(bot);
    // The view of the owner ends with the mission (Mind Vision of "mission watch").
    bot->RemoveAurasDueToSpell(2096);

    if (mission.bench)
    {
        uint32 count = 0;
        std::string items;
        for (auto const& [entry, n] : mission.items)
        {
            count += n;
            items += (items.empty() ? "" : ",") + std::to_string(entry) + "x" + std::to_string(n);
        }
        uint32 const minutes = mission.benchStartMs ? getMSTimeDiff(mission.benchStartMs, getMSTime()) / MINUTE / IN_MILLISECONDS : 0;
        LOG_INFO("playerbots.coa", "MISSION BENCH END {} lvl {} {} {}: {} items in {} min ({}), deaths {}, fails {}, zone {}: {}",
                 bot->GetName(), bot->GetLevel(), SkillName(mission.skillId), bot->GetSkillValue(mission.skillId), count,
                 minutes, items.empty() ? "-" : items, mission.deaths, mission.fails, bot->GetZoneId(), reason);
        // Its random bot strategies back, as the "reset" command does; not while it logs out.
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (botAI && bot->IsInWorld() && reason != "logged out")
            botAI->ResetStrategies();
        return;
    }

    ClearSaved(bot);

    uint32 const sent = Deliver(bot, mission.owner, mission.items);
    if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
    {
        std::ostringstream out;
        out << "Mission done: " << reason << ". ";
        if (sent)
            out << sent << " items sent to you by mail.";
        else
            out << "Nothing gathered.";
        botAI->TellMasterNoFacing(out.str());
    }
    RestoreStrategiesAndGroup(bot, mission.prevFlags);
}

void CoaMissionMgr::OnLoot(Player* bot, Item* item, uint32 count, ObjectGuid lootGuid)
{
    if (!active.load() || !bot || !item || !count || !lootGuid.IsGameObject())
        return;

    ItemTemplate const* proto = item->GetTemplate();
    if (!proto || proto->Class == ITEM_CLASS_QUEST || proto->Bonding == BIND_WHEN_PICKED_UP ||
        proto->Bonding == BIND_QUEST_ITEM)
        return;

    {
        std::lock_guard<std::mutex> guard(lock);
        if (missions.find(bot->GetGUID().GetCounter()) == missions.end())
            return;
    }

    if (!IsGatheringNode(ObjectAccessor::GetGameObject(*bot, lootGuid)))
        return;

    std::lock_guard<std::mutex> guard(lock);
    auto const it = missions.find(bot->GetGUID().GetCounter());
    if (it == missions.end())
        return;
    it->second.items[proto->ItemId] += count;
    it->second.lastLootMs = getMSTime();
}

uint32 CoaMissionMgr::MailNow(Player* bot)
{
    ObjectGuid::LowType owner = 0;
    std::map<uint32, uint32> items;
    {
        std::lock_guard<std::mutex> guard(lock);
        auto const it = missions.find(bot->GetGUID().GetCounter());
        if (it == missions.end() || it->second.bench)
            return 0;
        owner = it->second.owner;
        items.swap(it->second.items);
    }

    uint32 const sent = Deliver(bot, owner, items);
    {
        std::lock_guard<std::mutex> guard(lock);
        auto const it = missions.find(bot->GetGUID().GetCounter());
        if (it != missions.end())
        {
            it->second.mailed += sent;
            it->second.lastMailMs = getMSTime();
        }
    }
    Save(bot);
    return sent;
}

void CoaMissionMgr::OnDied(Player* bot)
{
    if (!active.load() || !bot)
        return;

    std::string endReason;
    {
        std::lock_guard<std::mutex> guard(lock);
        auto const it = missions.find(bot->GetGUID().GetCounter());
        if (it == missions.end())
            return;
        CoaMission& mission = it->second;
        ++mission.deaths;
        if (mission.risk && mission.state != CoaMissionState::Deliver)
            endReason = "I died in High Risk";
        else if (mission.deaths >= sPlayerbotAIConfig.missionsMaxDeaths && mission.state != CoaMissionState::Deliver)
            endReason = "I died " + std::to_string(mission.deaths) + " times";
    }
    // Ended here: a dead bot runs no mission step to end it.
    if (!endReason.empty())
    {
        Finish(bot, endReason);
        return;
    }
    Save(bot);
}

void CoaMissionMgr::OnBotLogout(Player* bot)
{
    if (!IsOnMission(bot))
        return;
    if (IsBench(bot))
    {
        Finish(bot, "logged out");
        return;
    }
    Save(bot);
    Remove(bot);
}

void CoaMissionMgr::OnBotLogin(Player* bot)
{
    PlayerbotAI* botAI = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
    if (!botAI)
        return;

    // Random bots never have a mission: no lookup for them.
    if (sRandomPlayerbotMgr.IsRandomBot(bot) || sPlayerbotAIConfig.IsInRandomAccountList(bot->GetSession()->GetAccountId()))
        return;

    ObjectGuid::LowType const guid = bot->GetGUID().GetCounter();
    if (!sRandomPlayerbotMgr.GetValue(guid, MISSION_KEY))
    {
        // Strategies saved during a mission that is over.
        if (botAI->HasStrategy("mission", BOT_STATE_NON_COMBAT))
        {
            bool const free = !botAI->HasStrategy("follow", BOT_STATE_NON_COMBAT) &&
                              !botAI->HasStrategy("stay", BOT_STATE_NON_COMBAT);
            botAI->ChangeStrategy(free ? "-mission,+follow" : "-mission", BOT_STATE_NON_COMBAT);
        }
        return;
    }

    std::vector<std::string> const fields = SplitFields(sRandomPlayerbotMgr.GetData(guid, MISSION_KEY), ';');
    CoaMission mission;
    try
    {
        // Version 1 (2.8.2 betas): no order.
        // 2.8.3: High Risk (risk, mailed) at the end.
        if (!(fields.size() == 15 && fields[0] == "1") &&
            !((fields.size() == 17 || fields.size() == 19) && fields[0] == MISSION_SAVE_VERSION))
            throw std::invalid_argument("format");
        mission.owner = std::stoul(fields[1]);
        mission.skillId = std::stoul(fields[2]);
        uint32 const state = std::stoul(fields[3]);
        mission.state = state == 2 ? CoaMissionState::Work : state == 3 ? CoaMissionState::Deliver : CoaMissionState::Travel;
        mission.remainingMs = int32(std::min<uint32>(std::stoul(fields[4]), 24 * HOUR) * IN_MILLISECONDS);
        mission.totalMinutes = std::stoul(fields[5]);
        mission.mapId = std::stoul(fields[6]);
        mission.x = std::stof(fields[7]);
        mission.y = std::stof(fields[8]);
        mission.z = std::stof(fields[9]);
        mission.zoneId = std::stoul(fields[10]);
        mission.point = std::stoul(fields[11]);
        mission.deaths = std::stoul(fields[12]);
        mission.fails = std::stoul(fields[13]);
        mission.prevFlags = std::stoul(fields[14]);
        if (fields.size() == 19)
        {
            mission.risk = fields[17] == "1";
            mission.mailed = std::stoul(fields[18]);
        }
        if (fields.size() >= 17)
        {
            mission.orderItem = std::stoul(fields[15]);
            mission.orderCount = std::stoul(fields[16]);
            uint32 skill = 0;
            uint32 req = 0;
            if (mission.orderItem)
                mission.orderNodes = TravelMgr::NodesYielding(mission.orderItem, skill, req);
        }
    }
    catch (std::exception const&)
    {
        LOG_ERROR("playerbots", "Mission of {} could not be read, dropped: {}", bot->GetName(),
                  sRandomPlayerbotMgr.GetData(guid, MISSION_KEY));
        ClearSaved(bot);
        return;
    }

    for (std::string const& part : SplitFields(sRandomPlayerbotMgr.GetData(guid, MISSION_ITEMS_KEY), ','))
    {
        std::vector<std::string> const pair = SplitFields(part, ':');
        if (pair.size() != 2)
            continue;
        try
        {
            mission.items[std::stoul(pair[0])] += std::stoul(pair[1]);
        }
        catch (std::exception const&)
        {
        }
    }

    // Only for the same owner; anyone else gets nothing and the items stay in the bags.
    Player* master = botAI->GetMaster();
    if (!master || !IsMissionBot(bot, master) || master->GetGUID().GetCounter() != mission.owner)
    {
        ClearSaved(bot);
        return;
    }

    TravelMgr::GatherCell const* cell = sTravelMgr.FindGatherCell(mission.mapId, mission.skillId, mission.x, mission.y);
    std::string reason;
    if (!sPlayerbotAIConfig.missionsEnable)
        reason = "missions are disabled on this server";
    else if (mission.remainingMs <= 0)
        reason = "the time was up";
    else if (mission.state == CoaMissionState::Deliver)
        reason = "it was over";
    else if (!cell)
        reason = "my gathering spot is gone";

    if (!reason.empty())
    {
        // Over while the bot was away: mail what it gathered now; the login puts it back in the group.
        ClearSaved(bot);
        uint32 const sent = Deliver(bot, mission.owner, mission.items);
        std::string change = "-mission";
        change += (mission.prevFlags & FLAG_FOLLOW) ? ",+follow" : "";
        change += (mission.prevFlags & FLAG_STAY) ? ",+stay" : "";
        change += (mission.prevFlags & FLAG_LFG) ? ",+lfg" : "";
        change += (mission.prevFlags & FLAG_GATHER_ALL) ? "" : ",-gather all";
        botAI->ChangeStrategy(change, BOT_STATE_NON_COMBAT);
        botAI->TellMasterNoFacing("Mission done: " + reason + ". " +
                                  (sent ? std::to_string(sent) + " items sent to you by mail." : "Nothing gathered."));
        return;
    }

    mission.point = mission.point < cell->points.size() ? mission.point : 0;
    mission.cell = cell;
    {
        // The owner's limit was checked when the mission began.
        std::lock_guard<std::mutex> guard(lock);
        missions[guid] = mission;
        active.store(uint32(missions.size()));
    }

    botAI->ChangeStrategy("-follow,-stay,-lfg,+loot,+gather,+gather all,+mission", BOT_STATE_NON_COMBAT);
    int32 const minutes = (mission.remainingMs + MINUTE * IN_MILLISECONDS - 1) / (MINUTE * IN_MILLISECONDS);
    botAI->TellMasterNoFacing("Mission resumed: " + SkillName(mission.skillId) + " in " + ZoneName(mission.zoneId) +
                              ", " + std::to_string(minutes) + " min left.");
}
