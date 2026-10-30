/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAMISSIONMGR_H
#define PLAYERBOTS_COAMISSIONMGR_H

#include "Common.h"
#include "ObjectGuid.h"
#include "TravelMgr.h"

#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>

class Engine;
class Item;
class Player;
class PlayerbotAI;

// Gathering missions of the alts: a player's own character, played as a bot, leaves the group, gathers herbs or
// ore in a zone of its level on its continent, then mails what it gathered to its owner.
// Whispered commands (CoaMissionAction): "mission gather [minutes] [mining|herbalism]", "mission status",
// "mission stop". Random bots never get one.
enum class CoaMissionState : uint8
{
    Travel = 1,   // walking to the gathering spot
    Work = 2,     // gathering
    Deliver = 3   // over: mail, give the strategies back, rejoin the group
};

struct CoaMission
{
    // Saved (V1.1: a mission resumes when the alt logs in again, after a restart too)
    ObjectGuid::LowType owner = 0;  // the real player whose character this is
    uint32 skillId = 0;             // SKILL_MINING or SKILL_HERBALISM
    CoaMissionState state = CoaMissionState::Travel;
    int32 remainingMs = 0;          // counts only while the alt is logged in
    uint32 totalMinutes = 0;
    uint32 mapId = 0;               // anchor of the gathering cell (TravelMgr::GatherCell)
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    uint32 zoneId = 0;
    uint32 point = 0;               // next spawn point of the cell to walk to
    uint32 deaths = 0;
    uint32 fails = 0;               // trips that made no progress, cells without a node
    uint32 prevFlags = 0;           // the non-combat strategies to give back at the end
    uint32 orderItem = 0;           // "mission order": the item ordered, and how many; 0 for a plain mission
    uint32 orderCount = 0;
    bool risk = false;              // "mission gather ... risk": in High Risk, mails on the way, over at the first death
    uint32 mailed = 0;              // items already mailed on the way (High Risk)
    std::string endReason;

    // Not saved
    std::set<uint32> orderNodes;    // the nodes that yield orderItem (TravelMgr::NodesYielding)
    bool bench = false;             // a random bot sent by .playerbots missiontest: no owner, nothing mailed
    uint32 benchStartMs = 0;
    TravelMgr::GatherCell const* cell = nullptr;  // TravelMgr's cells never move once loaded
    uint32 lastTickMs = 0;
    uint32 lastSaveMs = 0;
    uint32 lastGroupLeaveMs = 0;
    uint32 progressCheckMs = 0;
    float progressDist = 0.0f;
    uint32 workSinceMs = 0;
    ObjectGuid node;
    uint32 nodeSinceMs = 0;
    uint32 emptyPoints = 0;
    uint32 travelFails = 0;         // trips to a spot that made no progress
    uint32 lastMailMs = 0;
    uint32 lootClockMs = 0;         // start (or resume) of the mission: "nothing gathered for long" counts from it         // spawn points reached in a row with no node up
    std::set<ObjectGuid> skippedNodes;

    // Kept by the loot hook only: what the mission gathered, the only items it ever mails.
    std::map<uint32, uint32> items;
    uint32 lastLootMs = 0;
};

class CoaMissionMgr
{
public:
    static CoaMissionMgr& instance()
    {
        static CoaMissionMgr instance;
        return instance;
    }

    // The strategy flags saved in CoaMission::prevFlags.
    enum : uint32
    {
        FLAG_FOLLOW = 0x01,
        FLAG_STAY = 0x02,
        FLAG_LFG = 0x04,
        FLAG_LOOT = 0x08,
        FLAG_GATHER = 0x10,
        FLAG_GATHER_ALL = 0x20
    };

    bool IsOnMission(Player* bot) const;
    bool Get(Player* bot, CoaMission& out) const;
    // Writes the bot's part back; the gathered items, kept by the loot hook, stay as they are.
    void Put(Player* bot, CoaMission const& mission);
    // false (and why) when the owner already has the maximum number of missions.
    bool Begin(Player* bot, CoaMission const& mission, std::string& error);

    // Ends the mission at once: mails what was gathered, gives the strategies back, rejoins the owner's group.
    void Finish(Player* bot, std::string const& reason);

    // "MISSION;<state>;<zone>;<remaining minutes>;<items>;<skill>;<deaths>" or "MISSION;NONE".
    std::string StatusLine(Player* bot) const;

    // A bot that may get a mission: an alt whose master is the real player asking, never a random bot.
    static bool IsMissionBot(Player* bot, Player* requester);
    static std::string SkillName(uint32 skillId);
    // The non-combat strategies of a bot on a mission, kept through every strategy reset.
    static void ApplyEngineStrategies(Engine* nonCombatEngine, bool bench = false);
    bool IsBench(Player* bot) const;
    static uint32 ReadStrategyFlags(PlayerbotAI* botAI);

    void Save(Player* bot);
    // High Risk: mails now what was gathered so far; the mission goes on.
    uint32 MailNow(Player* bot);

    // Hooks
    void OnLoot(Player* bot, Item* item, uint32 count, ObjectGuid lootGuid);
    void OnDied(Player* bot);
    void OnBotLogin(Player* bot);
    void OnBotLogout(Player* bot);

private:
    CoaMissionMgr() = default;

    uint32 CountForOwner(ObjectGuid::LowType owner) const;
    void Remove(Player* bot);
    void ClearSaved(Player* bot);
    uint32 Deliver(Player* bot, ObjectGuid::LowType owner, std::map<uint32, uint32> const& items);
    void RestoreStrategiesAndGroup(Player* bot, uint32 prevFlags);

    mutable std::mutex lock;
    std::unordered_map<ObjectGuid::LowType, CoaMission> missions;
    std::atomic<uint32> active{0};
};

#define sCoaMissionMgr CoaMissionMgr::instance()

#endif
