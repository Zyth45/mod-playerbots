/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaMissionAction.h"

#include "AttackAction.h"
#include "Bag.h"
#include "CoaMissionMgr.h"
#include "Creature.h"
#include "DBCStores.h"
#include "Event.h"
#include "FishingAction.h"
#include "GameObject.h"
#include "LFGMgr.h"
#include "LootObjectStack.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "Timer.h"
#include "TravelMgr.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <sstream>

namespace
{
// CoA ruleset aura (core: AscensionRulesets.cpp): a lone gatherer would be killed again and again.
constexpr uint32 MISSION_HIGH_RISK_AURA = 1004019;
// Mind Vision: the player sees through the alt's eyes. An aura, so the core ends the view by itself when the alt
// leaves the map or logs out.
constexpr uint32 MISSION_WATCH_SPELL = 2096;

// The picks LootObject::IsLootPossible accepts for a mining node.
bool HasMiningPick(Player* bot)
{
    for (uint32 pick : {756u, 778u, 1819u, 1893u, 1959u, 2901u, 9465u, 20723u, 40772u, 40892u, 40893u})
        if (bot->HasItemCount(pick, 1))
            return true;
    return false;
}

// The knives LootObject::IsLootPossible accepts for a corpse to skin.
bool HasSkinningKnife(Player* bot)
{
    for (uint32 knife : {7005u, 12709u, 19901u, 40772u, 40893u})
        if (bot->HasItemCount(knife, 1))
            return true;
    return false;
}

bool IsFishingPole(Item* item)
{
    ItemTemplate const* proto = item ? item->GetTemplate() : nullptr;
    return proto && proto->Class == ITEM_CLASS_WEAPON && proto->SubClass == ITEM_SUBCLASS_WEAPON_FISHING_POLE;
}

// A fishing pole in its hands or its bags.
Item* FindFishingPole(Player* bot)
{
    if (Item* held = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND); IsFishingPole(held))
        return held;
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot); IsFishingPole(item))
            return item;
    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
        if (Bag* bag = bot->GetBagByPos(bagSlot))
            for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                if (Item* item = bag->GetItemByPos(uint8(slot)); IsFishingPole(item))
                    return item;
    return nullptr;
}

// The highest rank of Fishing it knows: a higher rank replaces the lower ones in the spell book.
uint32 FishingSpellOf(Player* bot)
{
    for (uint32 spell : {51294u, 33095u, 18248u, 7732u, 7731u, 7620u})
        if (bot->HasSpell(spell))
            return spell;
    return 0;
}

// The Skinning a corpse of this level asks for (as LootObject reads it).
uint32 SkinningSkillFor(uint32 level) { return level < 10 ? 1 : level < 20 ? (level - 10) * 10 : level * 5; }

// AttackAction::Attack for the mission: the target is set, the combat strategies do the rest.
class MissionAttackAction : public AttackAction
{
public:
    MissionAttackAction(PlayerbotAI* botAI) : AttackAction(botAI, "mission attack") {}

    bool Start(Unit* target) { return Attack(target); }
};

uint32 SkillOf(Player* bot, uint32 skillId) { return bot->HasSkill(skillId) ? bot->GetSkillValue(skillId) : 0; }

// The skill a spot is chosen for: the alt's level for cloth, which asks for none.
uint32 MissionSkillOf(Player* bot, uint32 skillId)
{
    return skillId == TravelMgr::GATHER_CLOTH ? bot->GetLevel() : SkillOf(bot, skillId);
}

std::string ZoneNameOf(uint32 zoneId)
{
    AreaTableEntry const* zone = sAreaTableStore.LookupEntry(zoneId);
    std::string const name = zone ? PlayerbotAI::GetLocalizedAreaName(zone) : "";
    return name.empty() ? "?" : name;
}

constexpr uint32 PROGRESS_CHECK_MS = 3 * MINUTE * IN_MILLISECONDS;
constexpr uint32 NODE_GIVE_UP_MS = MINUTE * IN_MILLISECONDS;
constexpr uint32 NO_NODE_MS = 10 * MINUTE * IN_MILLISECONDS;
constexpr uint32 SAVE_EVERY_MS = MINUTE * IN_MILLISECONDS;
constexpr uint32 MAX_FAILS = 3;
constexpr float TELEPORT_MIN_DIST = 400.0f;
// Nothing gathered for this long, from any spot: the mission ends.
constexpr uint32 NO_LOOT_END_MS = 20 * MINUTE * IN_MILLISECONDS;
// Skinning: rest before the next fight below this much health or mana.
constexpr float REST_HEALTH_PCT = 60.0f;
constexpr float REST_MANA_PCT = 40.0f;
// Fishing: casts with no catch before another water; time to reach the shore.
constexpr uint32 MAX_CASTS = 6;
constexpr uint32 SHORE_GIVE_UP_MS = 2 * MINUTE * IN_MILLISECONDS;
}  // namespace

bool CoaMissionAction::Execute(Event event)
{
    Player* requester = event.getOwner();

    std::string param = event.getParam();
    std::transform(param.begin(), param.end(), param.begin(), [](unsigned char c) { return std::tolower(c); });
    std::istringstream in(param);
    std::string word;
    in >> word;

    // Any other word stays unanswered: "mission" also starts sentences said in party chat.
    if (!word.empty() && word != "status" && word != "stop" && word != "gather" && word != "help" && word != "watch" &&
        word != "unwatch" && word != "order" && word != "can")
        return true;

    if (!sPlayerbotAIConfig.missionsEnable)
    {
        botAI->TellMasterNoFacing("Mission refused: missions are disabled on this server.");
        return true;
    }
    // Other bots of the group answer only a real request, not a status asked to the whole party.
    if (!CoaMissionMgr::IsMissionBot(bot, requester))
    {
        if (word == "gather")
            botAI->TellMasterNoFacing("Mission refused: missions are only for your own characters, asked by you.");
        return true;
    }

    if (word.empty() || word == "status")
    {
        botAI->TellMasterNoFacing(sCoaMissionMgr.StatusLine(bot));
        return true;
    }

    // "mission can": what this alt can be sent to do, for the addon:
    // "MISSIONCAN;<level>;<suggested>;<activity>:<skill>:<ok|nopick|noknife|nopole>,...", cloth always.
    if (word == "can")
    {
        botAI->TellMasterNoFacing(CanLine());
        return true;
    }

    // "mission watch": the camera of the player follows the alt, on the same continent only; "mission unwatch" ends it.
    if (word == "watch" || word == "unwatch")
    {
        Aura* const seen = bot->GetAura(MISSION_WATCH_SPELL, requester->GetGUID());
        if (word == "unwatch")
        {
            if (seen)
                bot->RemoveAura(seen);
            return true;
        }
        std::string refused;
        if (!sCoaMissionMgr.IsOnMission(bot))
            refused = "I am not on a mission";
        else if (requester->GetMapId() != bot->GetMapId())
            refused = "you are on another continent";
        else if (!seen && (requester->GetViewpoint() || requester->GetVehicle() || requester->GetCharmGUID()))
            refused = "you already see through other eyes";
        else if (!seen)
        {
            Aura* const aura = requester->AddAura(MISSION_WATCH_SPELL, bot);
            if (!aura)
                refused = "the view would not hold";
            else
            {
                aura->SetMaxDuration(-1);
                aura->SetDuration(-1);
            }
        }
        botAI->TellMasterNoFacing(refused.empty() ? "Watch on: " + ZoneNameOf(bot->GetZoneId()) + "."
                                                  : "Watch refused: " + refused + ".");
        return true;
    }

    // "mission order <item link or id> [count] [minutes]": gather that ore or herb until there are enough of it.
    if (word == "order")
    {
        std::string rest;
        std::getline(in, rest);
        uint32 item = 0;
        size_t const link = rest.find("item:");
        if (link != std::string::npos)
        {
            item = uint32(std::strtoul(rest.c_str() + link + 5, nullptr, 10));
            // The link's own numbers are not the count: drop it, from its colour to its end.
            size_t const from = rest.rfind("|c", link);
            size_t const to = rest.find("|r", link);
            rest.erase(from == std::string::npos ? link : from,
                       to == std::string::npos ? std::string::npos : to + 2 - (from == std::string::npos ? link : from));
        }
        std::vector<uint32> numbers;
        std::istringstream words(rest);
        std::string token;
        while (words >> token)
            if (!token.empty() && token.size() < 7 &&
                std::all_of(token.begin(), token.end(), [](unsigned char c) { return std::isdigit(c); }))
                numbers.push_back(uint32(std::stoul(token)));
        if (!item && !numbers.empty())
        {
            item = numbers.front();
            numbers.erase(numbers.begin());
        }
        uint32 const count = numbers.size() > 0 && numbers[0] ? std::min<uint32>(numbers[0], 1000) : 20;
        uint32 const minutes = numbers.size() > 1 ? numbers[1] : 0;

        std::string error;
        if (!item)
            botAI->TellMasterNoFacing("Usage: mission order <item link or id> [count] [minutes]");
        else if (!Start(requester, minutes, 0, error, item, count, rest.find(" risk") != std::string::npos))
            botAI->TellMasterNoFacing("Order refused: " + error + ".");
        return true;
    }

    if (word == "stop")
    {
        if (sCoaMissionMgr.IsOnMission(bot))
            sCoaMissionMgr.Finish(bot, "stopped by you");
        else
            botAI->TellMasterNoFacing(sCoaMissionMgr.StatusLine(bot));
        return true;
    }

    if (word == "help")
    {
        botAI->TellMasterNoFacing("Usage: mission gather [minutes] [mining|herbalism|skinning|fishing|cloth], mission order "
                                  "<item> [count] [minutes], mission status, mission stop");
        return true;
    }

    uint32 minutes = 0;
    uint32 skillId = 0;
    bool risk = false;
    std::string token;
    while (in >> token)
    {
        if (token == "risk")
            risk = true;
        if (!token.empty() && std::all_of(token.begin(), token.end(), [](unsigned char c) { return std::isdigit(c); }))
            minutes = token.size() > 5 ? 99999 : uint32(std::stoul(token));
        else if (token == "mining" || token == "mine" || token == "ore")
            skillId = SKILL_MINING;
        else if (token == "herbalism" || token == "herb" || token == "herbs")
            skillId = SKILL_HERBALISM;
        else if (token == "skinning" || token == "skin" || token == "leather")
            skillId = SKILL_SKINNING;
        else if (token == "fishing" || token == "fish")
            skillId = SKILL_FISHING;
        else if (token == "cloth")
            skillId = TravelMgr::GATHER_CLOTH;
    }

    std::string error;
    if (!Start(requester, minutes, skillId, error, 0, 0, risk))
        botAI->TellMasterNoFacing("Mission refused: " + error + ".");
    return true;
}

bool CoaMissionAction::Start(Player* requester, uint32 minutes, uint32 skillId, std::string& error, uint32 orderItem,
                             uint32 orderCount, bool risk)
{
    if (sCoaMissionMgr.IsOnMission(bot))
    {
        error = "I am already on a mission (whisper \"mission stop\" first)";
        return false;
    }
    if (!bot->IsAlive())
        error = "I am dead";
    else if (bot->IsInCombat())
        error = "I am in combat";
    else if (bot->IsInFlight())
        error = "I am on a flight";
    else if (!bot->GetMap() || bot->GetMap()->Instanceable())
        error = "I am in an instance";
    else if (bot->InBattleground() || bot->InArena() || bot->InBattlegroundQueue())
        error = "I am in a battleground or its queue";
    else if (sLFGMgr->GetState(bot->GetGUID()) != lfg::LFG_STATE_NONE)
        error = "I am in the dungeon finder";
    else if (bot->HasAura(MISSION_HIGH_RISK_AURA) && !sPlayerbotAIConfig.missionsHighRisk)
        error = "I am in High Risk";
    else if (bot->HasAura(MISSION_HIGH_RISK_AURA) && !risk)
        error = "I am in High Risk (add \"risk\" to accept: mission gather 60 risk)";
    if (!error.empty())
        return false;

    // An order: the profession is the one of the nodes that yield the item.
    std::set<uint32> orderNodes;
    if (orderItem)
    {
        uint32 reqSkill = 0;
        orderNodes = TravelMgr::NodesYielding(orderItem, skillId, reqSkill);
        if (orderNodes.empty())
        {
            error = "that item does not come from an ore, a herb, a beast to skin, fishing or cloth";
            return false;
        }
        if (SkillOf(bot, skillId) && SkillOf(bot, skillId) < reqSkill)
        {
            error = "I need " + std::to_string(reqSkill) + " in " + CoaMissionMgr::SkillName(skillId) + " for that";
            return false;
        }
    }

    // The profession: the one asked, else the best of mining (with a pick) and herbalism, then skinning (with a
    // knife), then fishing (with a pole), else cloth, which asks for none. The professions of CoA's own
    // (woodcutting...) are not gathered.
    bool const pick = HasMiningPick(bot);
    bool const knife = HasSkinningKnife(bot);
    bool const pole = FindFishingPole(bot) != nullptr;
    uint32 const mining = SkillOf(bot, SKILL_MINING);
    uint32 const herbalism = SkillOf(bot, SKILL_HERBALISM);
    uint32 const skinning = SkillOf(bot, SKILL_SKINNING);
    uint32 const fishing = FishingSpellOf(bot) ? SkillOf(bot, SKILL_FISHING) : 0;
    if (skillId == SKILL_MINING && !mining)
        error = "I do not know mining";
    else if (skillId == SKILL_MINING && !pick)
        error = "I have no mining pick";
    else if (skillId == SKILL_HERBALISM && !herbalism)
        error = "I do not know herbalism";
    else if (skillId == SKILL_SKINNING && !skinning)
        error = "I don't know skinning";
    else if (skillId == SKILL_SKINNING && !knife)
        error = "I have no skinning knife";
    else if (skillId == SKILL_FISHING && !fishing)
        error = "I don't know fishing";
    else if (skillId == SKILL_FISHING && !pole)
        error = "I have no fishing pole";
    else if (!skillId)
    {
        bool const canMine = mining && pick;
        if (canMine && (!herbalism || mining >= herbalism))
            skillId = SKILL_MINING;
        else if (herbalism)
            skillId = SKILL_HERBALISM;
        else if (skinning && knife)
            skillId = SKILL_SKINNING;
        else if (fishing && pole)
            skillId = SKILL_FISHING;
        else
            skillId = TravelMgr::GATHER_CLOTH;
    }
    if (!error.empty())
        return false;

    uint32 const skillValue = MissionSkillOf(bot, skillId);
    TravelMgr::GatherCell const* cell =
        sTravelMgr.SelectGatherCell(bot, skillId, skillValue, nullptr, orderItem ? &orderNodes : nullptr);
    // Fishing where no pool is known: the water right next to it, if the fish ordered (if any) is caught here.
    if (!cell && skillId == SKILL_FISHING &&
        (!orderItem || orderNodes.count(bot->GetAreaId()) || orderNodes.count(bot->GetZoneId())))
    {
        WorldPosition water =
            FindWaterRadial(bot, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMap(),
                            bot->GetPhaseMask(), 5.0f, 40.0f, 2.5f, false, 16);
        if (water.IsValid())
            cell = sTravelMgr.AddFishingCell(bot->GetMapId(), bot->GetZoneId(), water.GetPositionX(),
                                             water.GetPositionY(), water.GetPositionZ());
    }
    if (!cell && orderItem)
    {
        error = "I know no spot for that on this continent at my level";
        return false;
    }
    if (!cell)
    {
        error = skillId == SKILL_FISHING
                    ? "I know no fishing water for my level and skill on this continent: take me to some and ask again"
                    : "I know no " + CoaMissionMgr::SkillName(skillId) + " spot for my level and skill on this continent";
        return false;
    }

    if (!minutes)
        minutes = sPlayerbotAIConfig.missionsDefaultMinutes;
    minutes = std::min(minutes, sPlayerbotAIConfig.missionsMaxMinutes);

    CoaMission mission;
    mission.owner = requester ? requester->GetGUID().GetCounter() : 0;
    mission.bench = !requester;
    mission.benchStartMs = requester ? 0 : getMSTime();
    mission.skillId = skillId;
    mission.state = CoaMissionState::Travel;
    mission.remainingMs = int32(minutes * MINUTE * IN_MILLISECONDS);
    mission.totalMinutes = minutes;
    mission.mapId = cell->mapId;
    mission.x = cell->x;
    mission.y = cell->y;
    mission.z = cell->z;
    mission.zoneId = cell->zoneId;
    mission.cell = cell;
    mission.prevFlags = CoaMissionMgr::ReadStrategyFlags(botAI);
    mission.orderItem = orderItem;
    mission.orderCount = orderItem ? orderCount : 0;
    mission.orderNodes = orderNodes;
    mission.risk = bot->HasAura(MISSION_HIGH_RISK_AURA);

    if (!sCoaMissionMgr.Begin(bot, mission, error))
        return false;

    // Out of the group: neither Dungeon Clear nor the dungeon finder takes a bot that is not in it. The alt keeps its
    // master all the same.
    botAI->LeaveOrDisbandGroup();
    botAI->ChangeStrategy("-follow,-stay,-lfg,+loot,+gather,+gather all,+mission", BOT_STATE_NON_COMBAT);
    if (mission.bench)
    {
        botAI->ChangeStrategy("-grind,-rpg,-new rpg,-move random,-start duel,-bg,-quest", BOT_STATE_NON_COMBAT);
        LOG_INFO("playerbots.coa", "MISSION BENCH START {} lvl {} {} {} -> zone {} for {} min{}", bot->GetName(),
                 bot->GetLevel(), CoaMissionMgr::SkillName(skillId), skillValue, cell->zoneId, minutes,
                 orderItem ? " order " + std::to_string(orderCount) + "x" + std::to_string(orderItem) : "");
        return true;
    }
    sCoaMissionMgr.Save(bot);

    std::string const riskLine =
        mission.risk ? " High Risk: I mail what I gather on the way and stop at my first death." : "";
    if (orderItem)
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(orderItem);
        botAI->TellMasterNoFacing("Mission started: order of " + std::to_string(orderCount) + " " +
                                  (proto ? proto->Name1 : "?") + " (" + CoaMissionMgr::SkillName(skillId) + ") in " +
                                  ZoneNameOf(cell->zoneId) + ", at most " + std::to_string(minutes) +
                                  " min. I will mail them to you. Whisper \"mission status\" or \"mission stop\"." +
                                  riskLine);
        return true;
    }

    botAI->TellMasterNoFacing("Mission started: " + CoaMissionMgr::SkillName(skillId) + " in " +
                              ZoneNameOf(cell->zoneId) + " for " + std::to_string(minutes) +
                              " min. I will mail you what I gather. Whisper \"mission status\" or \"mission stop\"." +
                              riskLine);
    return true;
}

std::string CoaMissionAction::CanLine()
{
    bool const pick = HasMiningPick(bot);
    bool const knife = HasSkinningKnife(bot);
    bool const pole = FindFishingPole(bot) != nullptr;
    uint32 const mining = SkillOf(bot, SKILL_MINING);
    uint32 const herbalism = SkillOf(bot, SKILL_HERBALISM);
    uint32 const skinning = SkillOf(bot, SKILL_SKINNING);
    uint32 const fishing = FishingSpellOf(bot) ? SkillOf(bot, SKILL_FISHING) : 0;

    // The same choice as a mission sent with no activity.
    char const* suggested = "cloth";
    if (mining && pick && (!herbalism || mining >= herbalism))
        suggested = "mining";
    else if (herbalism)
        suggested = "herbalism";
    else if (skinning && knife)
        suggested = "skinning";
    else if (fishing && pole)
        suggested = "fishing";

    std::ostringstream out;
    out << "MISSIONCAN;" << uint32(bot->GetLevel()) << ';' << suggested << ';';
    if (mining)
        out << "mining:" << mining << ':' << (pick ? "ok" : "nopick") << ',';
    if (herbalism)
        out << "herbalism:" << herbalism << ":ok,";
    if (skinning)
        out << "skinning:" << skinning << ':' << (knife ? "ok" : "noknife") << ',';
    if (fishing)
        out << "fishing:" << fishing << ':' << (pole ? "ok" : "nopole") << ',';
    out << "cloth:" << uint32(bot->GetLevel()) << ":ok";
    return out.str();
}

bool CoaMissionUpdateAction::isUseful() { return bot->IsAlive() && sCoaMissionMgr.IsOnMission(bot); }

bool CoaMissionUpdateAction::Execute(Event /*event*/)
{
    CoaMission mission;
    if (!sCoaMissionMgr.Get(bot, mission))
        return false;

    uint32 const now = getMSTime();
    if (mission.lastTickMs)
        mission.remainingMs -= int32(std::min<uint32>(getMSTimeDiff(mission.lastTickMs, now), 10 * MINUTE * IN_MILLISECONDS));
    mission.lastTickMs = now;
    if (!mission.lastSaveMs)
        mission.lastSaveMs = now;

    // Safety: only for the alt of the real player who gave the mission, only in the open world.
    Player* master = botAI->GetMaster();
    if (!sPlayerbotAIConfig.missionsEnable)
    {
        sCoaMissionMgr.Finish(bot, "missions are disabled on this server");
        return true;
    }
    if (!mission.bench &&
        (!master || !CoaMissionMgr::IsMissionBot(bot, master) || master->GetGUID().GetCounter() != mission.owner))
    {
        sCoaMissionMgr.Finish(bot, "my owner changed");
        return true;
    }
    if (!bot->GetMap() || bot->GetMap()->Instanceable() || bot->InBattleground() || bot->InArena())
    {
        sCoaMissionMgr.Finish(bot, "I entered an instance");
        return true;
    }

    // Invited back meanwhile, or put back at login: out again, at most every 5 s.
    if (bot->GetGroup() && (!mission.lastGroupLeaveMs || getMSTimeDiff(mission.lastGroupLeaveMs, now) > 5000))
    {
        botAI->LeaveOrDisbandGroup();
        mission.lastGroupLeaveMs = now;
    }
    if (botAI->HasStrategy("follow", BOT_STATE_NON_COMBAT) || botAI->HasStrategy("stay", BOT_STATE_NON_COMBAT))
        botAI->ChangeStrategy("-follow,-stay", BOT_STATE_NON_COMBAT);

    if (!mission.cell)
        mission.cell = sTravelMgr.FindGatherCell(mission.mapId, mission.skillId, mission.x, mission.y);

    if (mission.state != CoaMissionState::Deliver)
    {
        if (mission.remainingMs <= 0)
        {
            mission.state = CoaMissionState::Deliver;
            mission.endReason = "the time is up";
        }
        else if (mission.orderItem && mission.items.count(mission.orderItem) &&
                 mission.items.at(mission.orderItem) >= mission.orderCount)
        {
            mission.state = CoaMissionState::Deliver;
            mission.endReason = "your order is ready";
        }
        else if (!bot->GetFreeInventorySpace())
        {
            mission.state = CoaMissionState::Deliver;
            mission.endReason = "my bags are full";
        }
        else if (!mission.cell)
        {
            mission.state = CoaMissionState::Deliver;
            mission.endReason = "my gathering spot is gone";
        }
    }

    // High Risk: what was gathered goes by mail every RiskMailEvery items or RiskMailMinutes, and at once when an
    // enemy player comes near.
    if (mission.risk && !mission.orderItem && mission.state != CoaMissionState::Deliver && !mission.bench)
    {
        if (!mission.lastMailMs)
            mission.lastMailMs = now;
        uint32 pending = 0;
        for (auto const& [entry, count] : mission.items)
            pending += count;
        bool const enemyNear = !AI_VALUE(GuidVector, "nearest enemy players").empty();
        uint32 const since = getMSTimeDiff(mission.lastMailMs, now);
        if (pending &&
            ((sPlayerbotAIConfig.missionsRiskMailEvery && pending >= sPlayerbotAIConfig.missionsRiskMailEvery) ||
             (sPlayerbotAIConfig.missionsRiskMailMinutes &&
              since >= sPlayerbotAIConfig.missionsRiskMailMinutes * MINUTE * IN_MILLISECONDS) ||
             (enemyNear && since >= MINUTE * IN_MILLISECONDS)))
        {
            uint32 const sent = sCoaMissionMgr.MailNow(bot);
            mission.items.clear();
            mission.mailed += sent;
            mission.lastMailMs = now;
            if (sent)
                botAI->TellMasterNoFacing("Mission mail: " + std::to_string(sent) + " items sent to you (High Risk" +
                                          (enemyNear ? ", an enemy player is near" : "") + ").");
        }
    }

    if (mission.state == CoaMissionState::Deliver)
    {
        std::string const reason = mission.endReason.empty() ? "it is over" : mission.endReason;
        sCoaMissionMgr.Put(bot, mission);
        sCoaMissionMgr.Finish(bot, reason);
        return true;
    }

    // Logged in or summoned away from the spot while gathering: travel back to it first.
    if (mission.state == CoaMissionState::Work &&
        (bot->GetMapId() != mission.mapId ||
         (bot->GetExactDist2d(mission.x, mission.y) > 300.0f && bot->GetZoneId() != mission.zoneId)))
    {
        mission.state = CoaMissionState::Travel;
        mission.progressCheckMs = 0;
        mission.workSinceMs = 0;
    }

    CoaMissionState const before = mission.state;
    bool const acted = mission.state == CoaMissionState::Travel ? DoTravel(mission, now) : DoWork(mission, now);

    if (mission.state != before)
        LOG_INFO("playerbots.coa", "Mission {}: {} -> {} (zone {}, {:.0f} yd from the spot, {} min left)", bot->GetName(),
                 uint32(before), uint32(mission.state), bot->GetZoneId(), bot->GetExactDist2d(mission.x, mission.y),
                 mission.remainingMs / MINUTE / IN_MILLISECONDS);
    bool const save = mission.state != before || getMSTimeDiff(mission.lastSaveMs, now) >= SAVE_EVERY_MS;
    if (save)
        mission.lastSaveMs = now;
    sCoaMissionMgr.Put(bot, mission);
    if (save)
        sCoaMissionMgr.Save(bot);
    return acted;
}

void CoaMissionUpdateAction::TeleportToSpot(CoaMission const& mission)
{
    if (bot->GetMapId() != mission.mapId && bot->HasAura(MISSION_WATCH_SPELL))
    {
        bot->RemoveAurasDueToSpell(MISSION_WATCH_SPELL);
        botAI->TellMasterNoFacing("Watch off: I am going to another continent.");
    }
    bot->StopMoving();
    bot->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TELEPORTED | AURA_INTERRUPT_FLAG_CHANGE_MAP);
    bot->TeleportTo(mission.mapId, mission.x, mission.y, mission.z + 0.5f, bot->GetOrientation());
}

bool CoaMissionUpdateAction::DoTravel(CoaMission& mission, uint32 now)
{
    if (bot->IsInFlight())
        return false;

    // The spot is always on the bot's continent; after a login elsewhere only a teleport gets it there.
    if (bot->GetMapId() != mission.mapId)
    {
        if (!sPlayerbotAIConfig.missionsTeleportFallback)
        {
            mission.state = CoaMissionState::Deliver;
            mission.endReason = "my gathering spot is on another continent";
            return false;
        }
        TeleportToSpot(mission);
        return true;
    }

    float const dist = bot->GetExactDist2d(mission.x, mission.y);
    // As when an alt is summoned: straight to the gathering spot, the mission time is spent gathering.
    // A next spot close by is reached on foot.
    if (dist >= TELEPORT_MIN_DIST && sPlayerbotAIConfig.missionsTeleportToSpot)
    {
        TeleportToSpot(mission);
        return true;
    }
    // Arrived: near the spot, or already in the mission's zone (an alt teleported there by its player gathers there).
    if (dist < 30.0f || (mission.zoneId && bot->GetZoneId() == mission.zoneId))
    {
        mission.state = CoaMissionState::Work;
        mission.workSinceMs = now;
        mission.progressCheckMs = 0;
        return false;
    }

    // No 20 yards gained in 3 minutes: a failure. Teleport if allowed, give up after the third.
    if (!mission.progressCheckMs)
    {
        mission.progressCheckMs = now;
        mission.progressDist = dist;
    }
    else if (getMSTimeDiff(mission.progressCheckMs, now) >= PROGRESS_CHECK_MS)
    {
        if (dist + 20.0f > mission.progressDist)
        {
            if (++mission.travelFails >= MAX_FAILS)
            {
                mission.state = CoaMissionState::Deliver;
                mission.endReason = "I could not reach my gathering spot";
                return false;
            }
            if (sPlayerbotAIConfig.missionsTeleportFallback)
            {
                mission.progressCheckMs = 0;
                TeleportToSpot(mission);
                return true;
            }
        }
        mission.progressCheckMs = now;
        mission.progressDist = dist;
    }

    return MoveFarTo(WorldPosition(mission.mapId, mission.x, mission.y, mission.z));
}

bool CoaMissionUpdateAction::DoWork(CoaMission& mission, uint32 now)
{
    TravelMgr::GatherCell const* cell = mission.cell;
    if (!cell || cell->points.empty())
        return false;

    if (mission.skillId == SKILL_FISHING)
        return DoFish(mission, now);

    // The loot strategy is on a node: let it gather, but not forever on one it cannot open or empty.
    if (bot->IsNonMeleeSpellCast(false))
        return false;
    ObjectGuid const looting = bot->GetLootGUID();
    LootObject target = AI_VALUE(LootObject, "loot target");
    ObjectGuid busy = looting ? looting : (!target.IsEmpty() && target.IsLootPossible(bot) ? target.guid : ObjectGuid::Empty);
    // The loot strategy picks a node again on its own: one already left is dropped at once.
    if (busy && !looting && mission.skippedNodes.count(busy))
    {
        AI_VALUE(LootObjectStack*, "available loot")->Remove(busy);
        context->GetValue<LootObject>("loot target")->Set(LootObject());
        busy.Clear();
    }
    if (busy)
    {
        mission.pointWalk = CoaMissionPoint::Progress();  // the walk to the point starts again after the node
        if (busy != mission.node)
        {
            mission.node = busy;
            mission.nodeSinceMs = now;
        }
        else if (getMSTimeDiff(mission.nodeSinceMs, now) >= NODE_GIVE_UP_MS)
        {
            if (looting)
                bot->GetSession()->DoLootRelease(looting);
            if (mission.skippedNodes.size() >= 50)
                mission.skippedNodes.clear();
            mission.skippedNodes.insert(busy);
            LOG_INFO("playerbots.coa", "Mission {}: node {} (entry {}) left after a minute without gathering it, skill {}",
                     bot->GetName(), busy.GetCounter(), busy.GetEntry(), SkillOf(bot, mission.skillId));
            AI_VALUE(LootObjectStack*, "available loot")->Remove(busy);
            context->GetValue<LootObject>("loot target")->Set(LootObject());
            mission.node.Clear();
        }
        return false;
    }

    // Skinning and cloth: the next beast or humanoid of its level, killed by the combat strategies, skinned or
    // looted by the loot strategy.
    if (mission.skillId == SKILL_SKINNING || mission.skillId == TravelMgr::GATHER_CLOTH)
    {
        if (Unit* prey = FindPrey(mission))
        {
            mission.emptyPoints = 0;
            return Hunt(mission, prey, now);
        }
    }
    else
    {
        // The nearest node of the mission's profession in sight.
        GameObject* best = nullptr;
        float bestDist = 0.0f;
        for (ObjectGuid const& guid : AI_VALUE(GuidVector, "nearest game objects no los"))
        {
            if (!guid.IsGameObject() || mission.skippedNodes.count(guid) ||
                (!mission.orderNodes.empty() && !mission.orderNodes.count(guid.GetEntry())))
                continue;
            GameObject* go = botAI->GetGameObject(guid);
            if (!go)
                continue;
            LootObject node(bot, guid);
            if (node.IsEmpty() || node.skillId != mission.skillId || !node.IsLootPossible(bot))
                continue;
            // Up a cave wall or under a ledge: right next to it and still out of reach.
            if (bot->GetExactDist2d(go) < 4.0f && std::fabs(go->GetPositionZ() - bot->GetPositionZ()) > 3.5f)
            {
                mission.skippedNodes.insert(guid);
                LOG_INFO("playerbots.coa", "Mission {}: node {} (entry {}) out of reach, {:.1f} yd above", bot->GetName(),
                         guid.GetCounter(), guid.GetEntry(), go->GetPositionZ() - bot->GetPositionZ());
                continue;
            }
            float const dist = bot->GetExactDist(go);
            if (!best || dist < bestDist)
            {
                best = go;
                bestDist = dist;
            }
        }

        if (best)
        {
            mission.emptyPoints = 0;
            mission.pointWalk = CoaMissionPoint::Progress();
            // A node it cannot reach in a minute (under water, up a cliff) is left for good.
            if (best->GetGUID() != mission.node)
            {
                mission.node = best->GetGUID();
                mission.nodeSinceMs = now;
            }
            else if (getMSTimeDiff(mission.nodeSinceMs, now) >= NODE_GIVE_UP_MS)
            {
                if (mission.skippedNodes.size() >= 50)
                    mission.skippedNodes.clear();
                mission.skippedNodes.insert(mission.node);
                mission.node.Clear();
                return false;
            }

            if (bestDist <= sPlayerbotAIConfig.lootDistance)
                AI_VALUE(LootObjectStack*, "available loot")->Add(best->GetGUID());
            if (bestDist > INTERACTION_DISTANCE - 2.0f)
                return MoveNear(best, 2.0f);
            return false;
        }
    }

    if (LeaveSpot(mission, now))
        return false;

    // Walk from spawn point to spawn point: only part of them is up at a time.
    if (mission.point >= cell->points.size())
        mission.point = 0;
    TravelMgr::GatherPoint const& point = cell->points[mission.point];
    float const pointDist = bot->GetExactDist(point.x, point.y, point.z);
    // Reached, or out of reach: a point it gets no closer to in a minute (in a cave with no path, up a ledge) is
    // skipped as an empty one; it stood still there until the spot was given up 10 minutes later (Redridge).
    if (pointDist < 8.0f || CoaMissionPoint::Stuck(mission.pointWalk, pointDist, now))
    {
        mission.point = (mission.point + 1) % cell->points.size();
        ++mission.emptyPoints;
        mission.pointWalk = CoaMissionPoint::Progress();
        return false;
    }
    return MoveFarTo(WorldPosition(cell->mapId, point.x, point.y, point.z));
}

bool CoaMissionUpdateAction::LeaveSpot(CoaMission& mission, uint32 now)
{
    TravelMgr::GatherCell const* cell = mission.cell;

    // Every spawn point visited with nothing up (gathered out, waiting for respawns), or
    // nothing gathered for 10 minutes: another spot. Only a spot that never gave anything is a failure.
    uint32 const since = std::max(mission.lastLootMs, mission.workSinceMs);
    bool const roundDone = mission.emptyPoints >= std::max<uint32>(3, cell->points.size());
    if (!roundDone && !(since && getMSTimeDiff(since, now) >= NO_NODE_MS))
        return false;

    bool const gaveLoot = mission.lastLootMs && mission.workSinceMs &&
                          int32(mission.lastLootMs - mission.workSinceMs) >= 0;
    // Spots that gave nothing in a row, for the log; the mission itself ends only after a long time with
    // nothing gathered anywhere: on a busy server the bot goes from spot to spot as the nodes come back.
    if (gaveLoot)
        mission.fails = 0;
    else
        ++mission.fails;
    uint32 const lastGather = std::max(mission.lastLootMs, mission.lootClockMs);
    if (lastGather && getMSTimeDiff(lastGather, now) >= NO_LOOT_END_MS)
    {
        mission.state = CoaMissionState::Deliver;
        mission.endReason = "I found nothing more to gather";
        return true;
    }
    TravelMgr::GatherCell const* next =
        sTravelMgr.SelectGatherCell(bot, mission.skillId, MissionSkillOf(bot, mission.skillId), cell,
                                    mission.orderNodes.empty() ? nullptr : &mission.orderNodes);
    if (!next)
    {
        // The only spot of its level: wait there for the nodes to come back.
        mission.emptyPoints = 0;
        if (since && getMSTimeDiff(since, now) >= NO_NODE_MS)
            mission.workSinceMs = now;
        return false;
    }
    LOG_INFO("playerbots.coa", "Mission {}: next spot ({}, {} empty points, fails {})", bot->GetName(),
             roundDone ? "spot gathered out" : "nothing gathered for 10 min", mission.emptyPoints, mission.fails);
    mission.cell = next;
    mission.pointWalk = CoaMissionPoint::Progress();
    mission.mapId = next->mapId;
    mission.x = next->x;
    mission.y = next->y;
    mission.z = next->z;
    mission.zoneId = next->zoneId;
    mission.point = 0;
    mission.emptyPoints = 0;
    mission.state = CoaMissionState::Travel;
    mission.progressCheckMs = 0;
    mission.workSinceMs = 0;
    mission.skippedNodes.clear();
    mission.fishSpot = false;
    mission.casts = 0;
    return true;
}

Unit* CoaMissionUpdateAction::FindPrey(CoaMission const& mission)
{
    bool const cloth = mission.skillId == TravelMgr::GATHER_CLOTH;
    uint32 const skill = SkillOf(bot, SKILL_SKINNING);
    Creature* best = nullptr;
    float bestDist = 0.0f;
    for (ObjectGuid const& guid : AI_VALUE(GuidVector, "possible targets no los"))
    {
        if (!guid.IsCreature() || mission.skippedNodes.count(guid) ||
            (!mission.orderNodes.empty() && !mission.orderNodes.count(guid.GetEntry())))
            continue;
        Creature* creature = botAI->GetCreature(guid);
        if (!creature || !creature->IsAlive() || creature->isElite() || creature->isWorldBoss() ||
            creature->GetOwnerGUID())
            continue;
        CreatureTemplate const* info = creature->GetCreatureTemplate();
        if (!info)
            continue;
        if (cloth ? !sTravelMgr.DropsCloth(creature->GetEntry())
                  : (!info->SkinLootId || info->GetRequiredLootSkill() != SKILL_SKINNING ||
                     SkinningSkillFor(creature->GetLevel()) > skill))
            continue;
        if (creature->GetLevel() > bot->GetLevel() + 1)
            continue;
        // Never one tapped or fought by someone else, nor one out of reach (in the water, on a ledge).
        if ((creature->hasLootRecipient() && !creature->isTappedBy(bot)) ||
            (creature->GetVictim() && creature->GetVictim() != bot))
            continue;
        if (creature->IsInWater() || std::fabs(creature->GetPositionZ() - bot->GetPositionZ()) > 10.0f)
            continue;
        if (!bot->IsValidAttackTarget(creature))
            continue;
        float const dist = bot->GetExactDist(creature);
        if (!best || dist < bestDist)
        {
            best = creature;
            bestDist = dist;
        }
    }
    return best;
}

bool CoaMissionUpdateAction::Hunt(CoaMission& mission, Unit* prey, uint32 now)
{
    // Rested first: the next fight starts with enough health and mana; eating and drinking are the bot's own.
    if (bot->GetHealthPct() < REST_HEALTH_PCT ||
        (bot->getPowerType() == POWER_MANA && bot->GetMaxPower(POWER_MANA) && bot->GetPowerPct(POWER_MANA) < REST_MANA_PCT))
        return false;

    // A beast it cannot get to fight in two minutes (evading, out of reach) is left for good.
    if (prey->GetGUID() != mission.node)
    {
        mission.node = prey->GetGUID();
        mission.nodeSinceMs = now;
    }
    else if (getMSTimeDiff(mission.nodeSinceMs, now) >= 2 * NODE_GIVE_UP_MS)
    {
        if (mission.skippedNodes.size() >= 50)
            mission.skippedNodes.clear();
        mission.skippedNodes.insert(mission.node);
        LOG_INFO("playerbots.coa", "Mission {}: beast {} (entry {}) left, not fought in two minutes", bot->GetName(),
                 prey->GetGUID().GetCounter(), prey->GetEntry());
        mission.node.Clear();
        return false;
    }

    if (bot->GetExactDist(prey) > 30.0f || !bot->IsWithinLOSInMap(prey))
        return MoveNear(prey, 5.0f);
    MissionAttackAction attack(botAI);
    return attack.Start(prey);
}

// A dry spot 11 to 17 yards from the water (x, y, z), in sight of it, the nearest to the bot.
bool CoaMissionUpdateAction::FindShore(CoaMission& mission, float wx, float wy, float wz)
{
    Map* map = bot->GetMap();
    if (!map)
        return false;
    uint32 const phase = bot->GetPhaseMask();
    bool found = false;
    float bestDist = 0.0f;
    for (float dist = 11.0f; dist <= 17.0f && !found; dist += 2.0f)
        for (int i = 0; i < 24; ++i)
        {
            float const angle = 2.0f * float(M_PI) * i / 24.0f;
            float const x = wx + std::cos(angle) * dist;
            float const y = wy + std::sin(angle) * dist;
            float const ground = HasFishableWaterOrLand(x, y, wz, map, phase, true);
            if (ground == INVALID_HEIGHT || std::fabs(ground - wz) > 6.0f)
                continue;
            if (!map->isInLineOfSight(x, y, ground + 2.0f, wx, wy, wz + 0.5f, phase, LINEOFSIGHT_ALL_CHECKS,
                                      VMAP::ModelIgnoreFlags::Nothing))
                continue;
            float const fromBot = bot->GetExactDist2d(x, y);
            if (!found || fromBot < bestDist)
            {
                found = true;
                bestDist = fromBot;
                mission.fishX = x;
                mission.fishY = y;
                mission.fishZ = ground;
            }
        }
    return found;
}

bool CoaMissionUpdateAction::DoFish(CoaMission& mission, uint32 now)
{
    TravelMgr::GatherCell const* cell = mission.cell;
    uint32 const spell = FishingSpellOf(bot);
    if (!spell)
    {
        mission.state = CoaMissionState::Deliver;
        mission.endReason = "I know fishing no more";
        return false;
    }

    // A bite: the line is pulled in at once, the catch comes as any loot.
    GameObject* bobber = bot->GetGameObject(spell);
    if (!bobber)
        for (ObjectGuid const& guid : AI_VALUE(GuidVector, "nearest game objects no los"))
            if (GameObject* go = botAI->GetGameObject(guid))
                if (go->GetGoType() == GAMEOBJECT_TYPE_FISHINGNODE && go->GetOwnerGUID() == bot->GetGUID())
                {
                    bobber = go;
                    break;
                }
    if (bobber && bobber->GetGoType() == GAMEOBJECT_TYPE_FISHINGNODE && bobber->getLootState() == GO_READY)
    {
        bobber->Use(bot);
        return true;
    }
    if (bot->IsNonMeleeSpellCast(false))
        return false;
    // A catch left open: closed after a while, so that it fishes again.
    if (ObjectGuid const looting = bot->GetLootGUID())
    {
        if (looting != mission.node)
        {
            mission.node = looting;
            mission.nodeSinceMs = now;
        }
        else if (getMSTimeDiff(mission.nodeSinceMs, now) >= 15 * IN_MILLISECONDS)
        {
            bot->GetSession()->DoLootRelease(looting);
            mission.node.Clear();
        }
        return false;
    }

    if (LeaveSpot(mission, now))
        return false;

    if (mission.point >= cell->points.size())
        mission.point = 0;
    TravelMgr::GatherPoint const& water = cell->points[mission.point];
    auto nextWater = [&]()
    {
        mission.fishSpot = false;
        mission.casts = 0;
        mission.point = (mission.point + 1) % cell->points.size();
        ++mission.emptyPoints;
    };

    if (!mission.fishSpot)
    {
        // The shore is looked for from close by: the ground of a far place is not loaded.
        if (bot->GetMapId() != cell->mapId || bot->GetExactDist2d(water.x, water.y) > 60.0f)
            return MoveFarTo(WorldPosition(cell->mapId, water.x, water.y, water.z));
        if (!FindShore(mission, water.x, water.y, water.z))
        {
            LOG_INFO("playerbots.coa", "Mission {}: no shore found by the water {} of the spot", bot->GetName(),
                     mission.point);
            nextWater();
            return false;
        }
        mission.fishSpot = true;
        mission.fishSinceMs = now;
        mission.casts = 0;
    }

    if (bot->GetExactDist(mission.fishX, mission.fishY, mission.fishZ) > 3.0f)
    {
        if (getMSTimeDiff(mission.fishSinceMs, now) >= SHORE_GIVE_UP_MS)
        {
            nextWater();
            return false;
        }
        return MoveTo(cell->mapId, mission.fishX, mission.fishY, mission.fishZ);
    }
    mission.fishSinceMs = now;

    // Casts with no catch since: no water where the bobber falls after all.
    if (mission.lastLootMs && mission.lastCastMs && int32(mission.lastLootMs - mission.lastCastMs) >= 0)
        mission.casts = 0;
    if (mission.casts >= MAX_CASTS)
    {
        LOG_INFO("playerbots.coa", "Mission {}: {} casts with no catch, next water", bot->GetName(), mission.casts);
        nextWater();
        return false;
    }

    // The pole in hand; the weapons it held go back at the end of the mission.
    Item* const mainHand = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
    if (!IsFishingPole(mainHand))
    {
        Item* pole = FindFishingPole(bot);
        if (!pole)
        {
            mission.state = CoaMissionState::Deliver;
            mission.endReason = "I have no fishing pole any more";
            return false;
        }
        Item* const offHand = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
        mission.weapon = mainHand ? mainHand->GetGUID() : ObjectGuid::Empty;
        mission.offhand = offHand ? offHand->GetGUID() : ObjectGuid::Empty;
        CoaMissionMgr::EquipItem(bot, pole, EQUIPMENT_SLOT_MAINHAND);
        // Refused (no room in the bags for what it held, a two-hand pole with an off-hand): no fishing at all.
        if (!IsFishingPole(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND)))
        {
            mission.state = CoaMissionState::Deliver;
            mission.endReason = "I could not take my fishing pole in hand (are my bags full?)";
            return false;
        }
        return true;
    }

    if (bot->isMoving())
    {
        bot->StopMoving();
        return true;
    }
    // Facing the water: the bobber falls 10 to 20 yards in front.
    bot->SetFacingTo(bot->GetAngle(water.x, water.y));
    ++mission.casts;
    mission.lastCastMs = now;
    return botAI->CastSpell(spell, bot);
}
