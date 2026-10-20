/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_RANDOMPLAYERBOTMGR_H
#define PLAYERBOTS_RANDOMPLAYERBOTMGR_H

#include "GameTime.h"
#include "NewRpgInfo.h"
#include "ObjectGuid.h"
#include "PlayerbotCommandServer.h"
#include "PlayerbotMgr.h"
#include <array>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

struct BattlegroundInfo
{
    std::vector<uint32> bgInstances;
    std::vector<uint32> ratedArenaInstances;
    std::vector<uint32> skirmishArenaInstances;
    uint32 bgInstanceCount = 0;
    uint32 ratedArenaInstanceCount = 0;
    uint32 skirmishArenaInstanceCount = 0;
    uint32 minLevel = 0;
    uint32 maxLevel = 0;
    uint32 activeRatedArenaQueue = 0;     // 0 = Inactive, 1 = Active
    uint32 activeSkirmishArenaQueue = 0;  // 0 = Inactive, 1 = Active
    uint32 activeBgQueue = 0;             // 0 = Inactive, 1 = Active

    // Bots (Arena)
    uint32 ratedArenaBotCount = 0;
    uint32 skirmishArenaBotCount = 0;

    // Bots (Battleground)
    uint32 bgHordeBotCount = 0;
    uint32 bgAllianceBotCount = 0;

    // Players (Arena)
    uint32 ratedArenaPlayerCount = 0;
    uint32 skirmishArenaPlayerCount = 0;

    // Players (Battleground)
    uint32 bgHordePlayerCount = 0;
    uint32 bgAlliancePlayerCount = 0;

    // Sum of the levels of the players and bots counted above, per faction (CoaBgLevelBalanceChance)
    uint32 bgHordeLevelSum = 0;
    uint32 bgAllianceLevelSum = 0;

    // Where a real player waits: how many more of each faction the core queue needs before the
    // battleground can start (its minimum per team), taken down as bots join.
    uint32 bgAllianceMissing = 0;
    uint32 bgHordeMissing = 0;

    // Rated solo queues (AiPlayerbot.CoaRatedSoloQueue), kept out of the rated team counts above: real players
    // and bots waiting there uninvited, by role, and how many more bots of each role complete their matches.
    uint32 ratedSoloPlayerHealers = 0;
    uint32 ratedSoloPlayerDamage = 0;
    uint32 ratedSoloBotHealers = 0;
    uint32 ratedSoloBotDamage = 0;
    uint32 ratedSoloHealersNeeded = 0;
    uint32 ratedSoloDamageNeeded = 0;
    // Bots a real player's other role may need (CoaRatedSoloRoleFallback), and bots no lineup needs:
    // these leave the queue after their usual wait, the 1v1 ones in the damage count.
    uint32 ratedSoloFallbackHealers = 0;
    uint32 ratedSoloFallbackDamage = 0;
    uint32 ratedSoloHealersSurplus = 0;
    uint32 ratedSoloDamageSurplus = 0;
    // Parties waiting there as one entry (rated "Join as Group"), and the slots their matches leave to others.
    uint32 ratedSoloPremades = 0;
    uint32 ratedSoloPremadeHealerSlots = 0;
    uint32 ratedSoloPremadeDamageSlots = 0;
};

// A party waiting in a rated solo 2v2 or 3v3 queue: its members, and how many of them are healers by their spec.
struct RatedSoloPremade
{
    uint32 queueTypeId;
    uint32 bracketId;
    uint32 players;
    uint32 healers;
};

// A real player waiting in a rated solo queue, and the role read from their spec; or a party waiting there,
// under its first member, and whether it was read as holding a healer.
struct RatedSoloWait
{
    ObjectGuid guid;
    uint32 joinTime;
    uint32 queueTypeId;
    uint32 bracketId;
    bool healer;
    bool party;
};

class ChatHandler;
class Group;
class PerfMonitorOperation;
class WorldLocation;

struct CachedEvent
{
    uint32 value = 0;
    uint32 lastChangeTime = 0;
    uint32 validIn = 0;
    std::string data;

    bool IsEmpty() const { return !lastChangeTime; }
};

struct BotEventCache
{
    bool loaded = false;
    std::unordered_map<std::string, CachedEvent> events;
};

// https://gist.github.com/bradley219/5373998

class botPIDImpl;
class botPID
{
public:
    // Kp -  proportional gain
    // Ki -  Integral gain
    // Kd -  derivative gain
    // dt -  loop interval time
    // max - maximum value of manipulated variable
    // min - minimum value of manipulated variable
    botPID(double dt, double max, double min, double Kp, double Ki, double Kd);
    void adjust(double Kp, double Ki, double Kd);
    void reset();

    double calculate(double setpoint, double pv);
    ~botPID();

private:
    botPIDImpl* pimpl;
};

class RandomPlayerbotMgr : public PlayerbotHolder
{
public:
    static RandomPlayerbotMgr& instance()
    {
        static RandomPlayerbotMgr instance;

        return instance;
    }

    void LogPlayerLocation();
    void UpdateAIInternal(uint32 elapsed, bool minimal = false) override;

    uint32 activeBots = 0;
    static bool HandlePlayerbotConsoleCommand(ChatHandler* handler, char const* args);
    bool IsRandomBot(Player* bot);
    bool IsRandomBot(ObjectGuid::LowType bot);
    bool IsAddclassBot(Player* bot);
    bool IsAddclassBot(ObjectGuid::LowType bot);
    void Randomize(Player* bot);
    void Clear(Player* bot);
    void RandomizeFirst(Player* bot);
    void RandomizeMin(Player* bot);
    void IncreaseLevel(Player* bot);
    void ScheduleTeleport(uint32 bot, uint32 time = 0);
    void ScheduleChangeStrategy(uint32 bot, uint32 time = 0);
    void HandleCommand(uint32 type, std::string const text, Player* fromPlayer, std::string channelName = "");
    std::string const HandleRemoteCommand(std::string const request);
    void OnPlayerLogout(Player* player);
    void OnPlayerLogin(Player* player);
    void OnPlayerLoginError(uint32 bot);
    // A group whose random bots wait for their player (AiPlayerbot.CoaOrphanGroupReleaseDelay): a character of a
    // player's account is in it and none of the players is connected, not even in a battleground or on a loading
    // screen. Never a battleground or dungeon finder group. Only reads, so map threads may ask too.
    bool CoaIsOrphanGroup(Group const* group);
    // World thread, every tick: every 10 s, the random bots of the groups orphaned for that delay leave them.
    void CoaReleaseOrphanGroups();
    Player* GetRandomPlayer();
    std::vector<Player*> GetPlayers() { return players; };
    PlayerBotMap GetAllBots() { return playerBots; };
    void InitArenaTeams();
    void PrintStats();
    double GetBuyMultiplier(Player* bot);
    double GetSellMultiplier(Player* bot);
    void AddTradeDiscount(Player* bot, Player* master, int32 value);
    void SetTradeDiscount(Player* bot, Player* master, uint32 value);
    uint32 GetTradeDiscount(Player* bot, Player* master);
    void Refresh(Player* bot);
    void RandomTeleportForLevel(Player* bot);
    void RandomTeleportGrindForLevel(Player* bot);
    void RandomTeleportForRpg(Player* bot);
    uint32 GetMaxAllowedBotCount();
    uint32 GetTargetBotCount();
    bool ProcessBot(Player* player);
    void Revive(Player* player);
    void ChangeStrategy(Player* player);
    void ChangeStrategyOnce(Player* player);
    uint32 GetValue(Player* bot, std::string const& type);
    uint32 GetValue(uint32 bot, std::string const& type);
    std::string GetData(uint32 bot, std::string const& type);
    void SetValue(uint32 bot, std::string const& type, uint32 value, std::string const& data = "");
    void SetValue(Player* bot, std::string const& type, uint32 value, std::string const& data = "");
    bool IsSpecPvp(uint32 bot, uint8 cls);
    // AiPlayerbot.CoaGearByContent: PvP or PvE gear for what the random bot does, at its grouping player's item level.
    void CoaUpdateGear(Player* bot);
    void Remove(Player* bot);
    ObjectGuid GetBattleMasterGUID(Player* bot, BattlegroundTypeId bgTypeId);
    CreatureData const* GetCreatureDataByEntry(uint32 entry);
    void LoadBattleMastersCache();
    std::map<uint32, std::map<uint32, BattlegroundInfo>> BattlegroundData;
    std::map<uint32, std::map<uint32, std::map<TeamId, uint32>>> VisualBots;
    std::map<uint32, std::map<uint32, std::map<uint32, uint32>>> Supporters;
    std::map<TeamId, std::vector<uint32>> LfgDungeons;
    // A random dungeon spans the whole range of its dungeons: true when a bot of this level is within a few
    // levels of the real players of its team waiting for that dungeon (or when none waits for it).
    bool IsNearLfgPlayerLevels(TeamId team, uint32 dungeonId, uint32 level);
    // A queued real player of this team still lacks a tank or a healer: damage dealers hold back.
    std::array<std::atomic<bool>, 2> LfgNeedTankOrHeal{};
    void CheckBgQueue();
    // How many healer and damage bots the rated solo queues real players wait in still need (CheckBgQueue).
    void CountRatedSoloNeeds();
    std::vector<RatedSoloWait> ratedSoloWaiting;
    std::unordered_map<uint32, RatedSoloPremade> ratedSoloPremades;  // by the leader's personal team
    // Real players given a bot of their other role: guid -> join time of that wait, and whether it was a healer.
    std::unordered_map<ObjectGuid, std::pair<uint32, bool>> ratedSoloFallbacks;
    // Last needs line of each rated solo queue and bracket: when, and what it said.
    std::unordered_map<uint32, std::pair<time_t, std::string>> ratedSoloNeedsLogged;
    void KeepArenaCaptainsOnline();
    void CheckLfgQueue();
    void CheckPlayers();
    void LogBattlegroundInfo();

    std::map<TeamId, std::map<BattlegroundTypeId, std::vector<uint32>>> getBattleMastersCache()
    {
        return BattleMastersCache;
    }

    float getActivityMod() { return activityMod; }
    float getActivityPercentage() { return activityMod * 100.0f; }
    void setActivityPercentage(float percentage) { activityMod = percentage / 100.0f; }
    static uint8 GetTeamClassIdx(bool isAlliance, uint8 claz) { return isAlliance * 20 + claz; }

    void PrepareAddclassCache();
    void Init();
    std::map<uint8, std::unordered_set<ObjectGuid>> addclassCache;

    // Account type management
    void AssignAccountTypes();
    bool IsAccountType(uint32 accountId, uint8 accountType);

protected:
    void OnBotLoginInternal(Player* const bot) override;
    // AiPlayerbot.CoaRulesetForBots: puts a random bot in its High Risk / War Mode / PvE ruleset (see the .dist).
    void CoaApplyRuleset(Player* bot);

private:
    RandomPlayerbotMgr() : PlayerbotHolder()
    {
        this->playersLevel = sPlayerbotAIConfig.randombotStartingLevel;

        if (sPlayerbotAIConfig.enabled || sPlayerbotAIConfig.randomBotAutologin)
        {
            PlayerbotCommandServer::instance().Start();
        }

        BattlegroundData.clear();  // Clear here and here only.

        // Cleanup on server start: orphaned pet data that's often left behind by bot pets that no longer exist in the DB
        CharacterDatabase.Execute("DELETE FROM pet_aura WHERE guid NOT IN (SELECT id FROM character_pet)");
        CharacterDatabase.Execute("DELETE FROM pet_spell WHERE guid NOT IN (SELECT id FROM character_pet)");
        CharacterDatabase.Execute("DELETE FROM pet_spell_cooldown WHERE guid NOT IN (SELECT id FROM character_pet)");

        for (int bracket = BG_BRACKET_ID_FIRST; bracket < MAX_BATTLEGROUND_BRACKETS; ++bracket)
        {
            for (int queueType = BATTLEGROUND_QUEUE_AV; queueType < MAX_BATTLEGROUND_QUEUE_TYPES; ++queueType)
            {
                this->BattlegroundData[queueType][bracket] = BattlegroundInfo();
            }
        }

        this->BgCheckTimer = 0;
        this->LfgCheckTimer = 0;
        this->PlayersCheckTimer = 0;
    }

    ~RandomPlayerbotMgr() = default;

    RandomPlayerbotMgr(RandomPlayerbotMgr const&) = delete;
    RandomPlayerbotMgr& operator=(RandomPlayerbotMgr const&) = delete;

    RandomPlayerbotMgr(RandomPlayerbotMgr&&) = delete;
    RandomPlayerbotMgr& operator=(RandomPlayerbotMgr&&) = delete;

    // pid values are set in constructor
    botPID pid = botPID(1, 50, -50, 0, 0, 0);
    float activityMod = 0.25;
    bool _isBotInitializing = true;
    bool _isBotLogging = true;
    NewRpgStatistic rpgStasticTotal;
    CachedEvent* FindEvent(uint32 bot, std::string const& event);
    uint32 GetEventValue(uint32 bot, std::string const& event);
    std::string GetEventData(uint32 bot, std::string const& event);
    uint32 SetEventValue(uint32 bot, std::string const& event, uint32 value, uint32 validIn,
                         std::string const& data = "");
    void GetBots();
    std::vector<uint32> GetBgBots(uint32 bracket);
    time_t BgCheckTimer;
    time_t ArenaCaptainCheckTimer = 0;
    time_t LfgCheckTimer;
    time_t PlayersCheckTimer;
    time_t RealPlayerLastTimeSeen = 0;
    time_t DelayLoginBotsTimer;
    time_t printStatsTimer;
    std::unordered_map<ObjectGuid::LowType, time_t> coaOrphanGroups;  // group -> since when it has been orphaned
    time_t coaOrphanCheckAt = 0;
    uint32 AddRandomBots();
    void CoaTrimBotsForPlayers(uint32 target);
    bool ProcessBot(uint32 bot);
    void ScheduleRandomize(uint32 bot, uint32 time);
    void RandomTeleport(Player* bot);
    void RandomTeleport(Player* bot, std::vector<WorldLocation>& locs, bool hearth = false);
    uint32 GetZoneLevel(uint16 mapId, float teleX, float teleY, float teleZ);
    std::vector<WorldLocation> GetPlayerZoneTeleportLocations(std::vector<WorldLocation> const& locs, Player* bot);
    typedef void (RandomPlayerbotMgr::*ConsoleCommandHandler)(Player*);
    std::vector<Player*> players;

    // std::map<uint32, std::vector<WorldLocation>> rpgLocsCache;
    std::map<uint32, std::map<uint32, std::vector<WorldLocation>>> rpgLocsCacheLevel;
    std::map<TeamId, std::map<BattlegroundTypeId, std::vector<uint32>>> BattleMastersCache;
    std::unordered_map<uint32, ObjectGuid::LowType> BattleMasterSpawnIds;
    // Lowest and highest level of the real players of a team waiting for a dungeon. Written by CheckLfgQueue,
    // read by the bots from the map update threads.
    std::map<TeamId, std::map<uint32, std::pair<uint8, uint8>>> lfgPlayerLevels;
    std::mutex lfgPlayerLevelsLock;
    // Bot AI reads and writes these values from the map update threads.
    std::mutex eventCacheLock;
    std::unordered_map<uint32, BotEventCache> eventCache;
    std::unordered_set<uint32> currentBots;
    uint32 playersLevel;

    // Account lists
    std::vector<uint32> rndBotTypeAccounts;             // Accounts marked as RNDbot (type 1)
    std::vector<uint32> addClassTypeAccounts;           // Accounts marked as AddClass (type 2)

    //void ScaleBotActivity();      // Deprecated function
    static inline uint32 NowSeconds() { return static_cast<uint32>(GameTime::GetGameTime().count()); }
};

#define sRandomPlayerbotMgr RandomPlayerbotMgr::instance()

#endif
