/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "RandomPlayerbotMgr.h"
#include "BattlegroundUtils.h"
#include "AiFactory.h"
#include "ArenaTeam.h"
#include "ArenaTeamMgr.h"
#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "Cell.h"
#include "CellImpl.h"
#include "ChannelMgr.h"
#include "DBCStores.h"
#include "DBCStructure.h"
#include "DatabaseEnv.h"
#include "Define.h"
#include "FleeManager.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "LFGMgr.h"
#include "MapMgr.h"
#include "NewRpgInfo.h"
#include "NewRpgStrategy.h"
#include "ObjectGuid.h"
#include "PerfMonitor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "CoaSpecialization.h"
#include "AscensionSpecialization.h"
#include "SpellMgr.h"
#include "PlayerbotTextMgr.h"
#include "Playerbots.h"
#include "Position.h"
#include "RaceMgr.h"
#include "Random.h"
#include "RandomBotLevelMgr.h"
#include "RandomPlayerbotFactory.h"
#include "ServerFacade.h"
#include "SharedDefines.h"
#include "TravelMgr.h"
#include "Unit.h"
#include "World.h"
#include "WorldSessionMgr.h"
#include <algorithm>
#include <boost/thread/thread.hpp>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <random>
#include <set>
#include <utility>

struct GuidClassRaceInfo
{
    ObjectGuid::LowType guid;
    uint32 rClass;
    uint32 rRace;
};

void PrintStatsThread() { sRandomPlayerbotMgr.PrintStats(); }

void activatePrintStatsThread()
{
    boost::thread t(PrintStatsThread);
    t.detach();
}

void CheckBgQueueThread() { sRandomPlayerbotMgr.CheckBgQueue(); }

void activateCheckBgQueueThread()
{
    boost::thread t(CheckBgQueueThread);
    t.detach();
}

void CheckLfgQueueThread() { sRandomPlayerbotMgr.CheckLfgQueue(); }

void activateCheckLfgQueueThread()
{
    boost::thread t(CheckLfgQueueThread);
    t.detach();
}

void CheckPlayersThread() { sRandomPlayerbotMgr.CheckPlayers(); }

void activateCheckPlayersThread()
{
    boost::thread t(CheckPlayersThread);
    t.detach();
}

class botPIDImpl
{
public:
    botPIDImpl(double dt, double max, double min, double Kp, double Ki, double Kd);
    ~botPIDImpl();
    double calculate(double setpoint, double pv);
    void adjust(double Kp, double Ki, double Kd)
    {
        _Kp = Kp;
        _Ki = Ki;
        _Kd = Kd;
    }
    void reset() { _integral = 0; }

private:
    double _dt;
    double _max;
    double _min;
    double _Kp;
    double _Ki;
    double _Kd;
    double _pre_error;
    double _integral;
};

botPID::botPID(double dt, double max, double min, double Kp, double Ki, double Kd)
{
    pimpl = new botPIDImpl(dt, max, min, Kp, Ki, Kd);
}
void botPID::adjust(double Kp, double Ki, double Kd) { pimpl->adjust(Kp, Ki, Kd); }
void botPID::reset() { pimpl->reset(); }
double botPID::calculate(double setpoint, double pv) { return pimpl->calculate(setpoint, pv); }
botPID::~botPID() { delete pimpl; }

/**
 * Implementation
 */
botPIDImpl::botPIDImpl(double dt, double max, double min, double Kp, double Ki, double Kd)
    : _dt(dt), _max(max), _min(min), _Kp(Kp), _Ki(Ki), _Kd(Kd), _pre_error(0), _integral(0)
{
}

double botPIDImpl::calculate(double setpoint, double pv)
{
    // Calculate error
    double error = setpoint - pv;

    // Proportional term
    double Pout = _Kp * error;

    // Integral term
    _integral += error * _dt;
    double Iout = _Ki * _integral;

    // Derivative term
    double derivative = (error - _pre_error) / _dt;
    double Dout = _Kd * derivative;

    // Calculate total output
    double output = Pout + Iout + Dout;

    // Restrict to max/min
    if (output > _max)
    {
        output = _max;
        _integral -= error * _dt;  // Stop integral buildup at max
    }
    else if (output < _min)
    {
        output = _min;
        _integral -= error * _dt;  // Stop integral buildup at min
    }

    // Save error to previous error
    _pre_error = error;

    return output;
}

botPIDImpl::~botPIDImpl() {}

uint32 RandomPlayerbotMgr::GetMaxAllowedBotCount() { return GetTargetBotCount(); }

// AiPlayerbot.CoaPlayersReplaceBots: real players take the place of random bots, so the world keeps about the same
// population. At 100 (%), 500 bots and 40 players online leave 460 bots; never fewer than CoaPlayersReplaceBotsMin.
// Alt bots and other bots of a real player are not random bots and are never counted or removed.
uint32 RandomPlayerbotMgr::GetTargetBotCount()
{
    uint32 const count = GetEventValue(0, "bot_count");
    if (!sPlayerbotAIConfig.coaPlayersReplaceBots || !count)
        return count;

    uint32 const players = sWorldSessionMgr->GetActiveSessionCount();
    uint32 const replaced = players * sPlayerbotAIConfig.coaPlayersReplaceBots / 100;
    uint32 const floor = std::min(count, sPlayerbotAIConfig.coaPlayersReplaceBotsMin);
    return replaced >= count - floor ? floor : count - replaced;
}

// Sends random bots home while more are in the world than the players leave room for, a few per pass. A margin keeps
// a player logging in and out from swapping bots each time. Bots grouped, in a dungeon, a battleground, an arena or a
// queue stay: ProcessBot() then logs out the ones marked here.
void RandomPlayerbotMgr::CoaTrimBotsForPlayers(uint32 target)
{
    if (!sPlayerbotAIConfig.coaPlayersReplaceBots)
        return;

    // Bots already leaving stay in currentBots until ProcessBot() logs them out: not counted again.
    uint32 staying = 0;
    for (uint32 const bot : currentBots)
        if (GetEventValue(bot, "add"))
            ++staying;

    if (staying <= target + 2 + target / 100)
        return;

    uint32 const toRemove =
        std::min<uint32>(staying - target, std::max<uint32>(1, sPlayerbotAIConfig.randomBotsPerInterval));
    std::vector<uint32> picked;
    for (uint32 const bot : currentBots)
    {
        if (picked.size() >= toRemove)
            break;

        if (!GetEventValue(bot, "add"))
            continue;  // already leaving

        Player* player = GetPlayerBot(ObjectGuid::Create<HighGuid::Player>(bot));
        if (player && (player->GetGroup() || player->InBattleground() || player->InArena() ||
                       player->InBattlegroundQueue() || !player->IsInWorld() || player->GetMap()->IsDungeon()))
            continue;

        picked.push_back(bot);
    }

    for (uint32 const bot : picked)
    {
        SetEventValue(bot, "add", 0, 0);
        if (!GetPlayerBot(ObjectGuid::Create<HighGuid::Player>(bot)))
            currentBots.erase(bot);  // not in the world yet: nothing to log out
    }

    if (!picked.empty())
        LOG_INFO("playerbots", "{} random bots leave for real players ({} staying, {} wanted)", picked.size(),
                 staying - picked.size(), target);
}

void RandomPlayerbotMgr::LogPlayerLocation()
{
    activeBots = 0;

    try
    {
        sPlayerbotAIConfig.openLog("player_location.csv", "w");

        if (sPlayerbotAIConfig.randomBotAutologin)
        {
            for (auto i : GetAllBots())
            {
                Player* bot = i.second;
                if (!bot)
                    continue;

                std::ostringstream out;
                out << sPlayerbotAIConfig.GetTimestampStr() << "+00,";
                out << "RND"
                    << ",";
                out << bot->GetName() << ",";
                out << std::fixed << std::setprecision(2);
                WorldPosition(bot).printWKT(out);
                out << bot->GetOrientation() << ",";
                out << std::to_string(bot->getRace()) << ",";
                out << std::to_string(bot->getClass()) << ",";
                out << bot->GetMapId() << ",";
                out << bot->GetLevel() << ",";
                out << bot->GetHealth() << ",";
                out << bot->GetPowerPct(bot->getPowerType()) << ",";
                out << bot->GetMoney() << ",";

                if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
                {
                    out << std::to_string(uint8(botAI->GetGrouperType())) << ",";
                    out << std::to_string(uint8(botAI->GetGuilderType())) << ",";
                    out << (botAI->AllowActivity(ALL_ACTIVITY) ? "active" : "inactive") << ",";
                    out << (botAI->IsActive() ? "active" : "delay") << ",";
                    out << botAI->HandleRemoteCommand("state") << ",";

                    if (botAI->AllowActivity(ALL_ACTIVITY))
                        activeBots++;
                }
                else
                {
                    out << 0 << "," << 0 << ",err,err,err,";
                }

                out << (bot->IsInCombat() ? "combat" : "safe") << ",";
                out << (bot->isDead() ? (bot->GetCorpse() ? "ghost" : "dead") : "alive");

                sPlayerbotAIConfig.log("player_location.csv", out.str().c_str());
            }

            for (auto i : GetPlayers())
            {
                Player* bot = i;
                if (!bot)
                    continue;

                std::ostringstream out;
                out << sPlayerbotAIConfig.GetTimestampStr() << "+00,";
                out << "PLR"
                    << ",";
                out << bot->GetName() << ",";
                out << std::fixed << std::setprecision(2);
                WorldPosition(bot).printWKT(out);
                out << bot->GetOrientation() << ",";
                out << std::to_string(bot->getRace()) << ",";
                out << std::to_string(bot->getClass()) << ",";
                out << bot->GetMapId() << ",";
                out << bot->GetLevel() << ",";
                out << bot->GetHealth() << ",";
                out << bot->GetPowerPct(bot->getPowerType()) << ",";
                out << bot->GetMoney() << ",";

                if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
                {
                    out << std::to_string(uint8(botAI->GetGrouperType())) << ",";
                    out << std::to_string(uint8(botAI->GetGuilderType())) << ",";
                    out << (botAI->AllowActivity(ALL_ACTIVITY) ? "active" : "inactive") << ",";
                    out << (botAI->IsActive() ? "active" : "delay") << ",";
                    out << botAI->HandleRemoteCommand("state") << ",";

                    if (botAI->AllowActivity(ALL_ACTIVITY))
                        activeBots++;
                }
                else
                {
                    out << 0 << "," << 0 << ",player,player,player,";
                }

                out << (bot->IsInCombat() ? "combat" : "safe") << ",";
                out << (bot->isDead() ? (bot->GetCorpse() ? "ghost" : "dead") : "alive");

                sPlayerbotAIConfig.log("player_location.csv", out.str().c_str());
            }
        }
    }
    catch (...)
    {
        return;
        // This is to prevent some thread-unsafeness. Crashes would happen if bots get added or removed.
        // We really don't care here. Just skip a log. Making this thread-safe is not worth the effort.
    }
}

void RandomPlayerbotMgr::UpdateAIInternal(uint32 /*elapsed*/, bool /*minimal*/)
{
    if (totalPmo)
        totalPmo->finish();

    totalPmo = sPerfMonitor.start(PERF_MON_TOTAL, "RandomPlayerbotMgr::FullTick");

    if (!sPlayerbotAIConfig.randomBotAutologin || !sPlayerbotAIConfig.enabled)
        return;

    /*if (sPlayerbotAIConfig.enablePrototypePerformanceDiff)
    {
        LOG_INFO("playerbots", "---------------------------------------");
        LOG_INFO("playerbots",
                 "PROTOTYPE: Playerbot performance enhancements are active. Issues and instability may occur.");
        LOG_INFO("playerbots", "---------------------------------------");
        ScaleBotActivity();
    }*/

    uint32 maxAllowedBotCount = GetEventValue(0, "bot_count");
    if (!maxAllowedBotCount || (maxAllowedBotCount < sPlayerbotAIConfig.minRandomBots ||
                                maxAllowedBotCount > sPlayerbotAIConfig.maxRandomBots))
    {
        maxAllowedBotCount = urand(sPlayerbotAIConfig.minRandomBots, sPlayerbotAIConfig.maxRandomBots);
        SetEventValue(0, "bot_count", maxAllowedBotCount,
                      urand(sPlayerbotAIConfig.randomBotCountChangeMinInterval,
                            sPlayerbotAIConfig.randomBotCountChangeMaxInterval));
    }

    maxAllowedBotCount = GetTargetBotCount();

    GetBots();
    CoaTrimBotsForPlayers(maxAllowedBotCount);
    // Copied deliberately: ProcessBot() erases from currentBots while this is
    // being iterated below, so the loops must run over a snapshot.
    std::unordered_set<uint32> availableBots = currentBots;
    uint32 availableBotCount = availableBots.size();
    uint32 onlineBotCount = playerBots.size();

    uint32 onlineBotFocus = 75;
    if (onlineBotCount < (uint32)(sPlayerbotAIConfig.minRandomBots * 90 / 100))
        onlineBotFocus = 25;

    // only keep updating till initializing time has completed,
    // which prevents unneeded expensive GameTime calls.
    if (_isBotInitializing)
    {
        _isBotInitializing = GameTime::GetUptime().count() < sPlayerbotAIConfig.maxRandomBots * (0.11 + 0.4);
    }

    uint32 updateIntervalTurboBoost = _isBotInitializing ? 1 : sPlayerbotAIConfig.randomBotUpdateInterval;
    SetNextCheckDelay(updateIntervalTurboBoost * (onlineBotFocus + 25) * 10);

    PerfMonitorOperation* pmo = sPerfMonitor.start(
        PERF_MON_TOTAL,
        onlineBotCount < maxAllowedBotCount ? "RandomPlayerbotMgr::Login" : "RandomPlayerbotMgr::UpdateAIInternal");

    bool realPlayerIsLogged = false;
    if (sPlayerbotAIConfig.disabledWithoutRealPlayer)
    {
        if (sWorldSessionMgr->GetActiveAndQueuedSessionCount() > 0)
        {
            RealPlayerLastTimeSeen = time(nullptr);
            realPlayerIsLogged = true;

            if (DelayLoginBotsTimer == 0)
            {
                DelayLoginBotsTimer = time(nullptr) + sPlayerbotAIConfig.disabledWithoutRealPlayerLoginDelay;
            }
        }
        else
        {
            if (DelayLoginBotsTimer)
            {
                DelayLoginBotsTimer = 0;
            }

            if (RealPlayerLastTimeSeen != 0 && onlineBotCount > 0 &&
                time(nullptr) > RealPlayerLastTimeSeen + sPlayerbotAIConfig.disabledWithoutRealPlayerLogoutDelay)
            {
                LogoutAllBots();
                LOG_INFO("playerbots", "Logout all bots due no real player session.");
            }
        }

        if (availableBotCount < maxAllowedBotCount &&
            (sPlayerbotAIConfig.disabledWithoutRealPlayer == false ||
             (realPlayerIsLogged && DelayLoginBotsTimer != 0 && time(nullptr) >= DelayLoginBotsTimer)))
        {
            AddRandomBots();
        }
    }
    else if (availableBotCount < maxAllowedBotCount)
    {
        AddRandomBots();
    }

    if (sPlayerbotAIConfig.syncLevelWithPlayers && !players.empty())
    {
        if (time(nullptr) > (PlayersCheckTimer + 60))
            sRandomPlayerbotMgr.CheckPlayers();
    }

    if (sPlayerbotAIConfig.randomBotJoinBG /* && !players.empty()*/)
    {
        if (time(nullptr) > (BgCheckTimer + 35))
            sRandomPlayerbotMgr.CheckBgQueue();

        if (time(nullptr) > (ArenaCaptainCheckTimer + 60) &&
            (!sPlayerbotAIConfig.disabledWithoutRealPlayer || realPlayerIsLogged))
            KeepArenaCaptainsOnline();
    }

    if (sPlayerbotAIConfig.randomBotJoinLfg /* && !players.empty()*/)
    {
        if (time(nullptr) > (LfgCheckTimer + 30))
            sRandomPlayerbotMgr.CheckLfgQueue();
    }

    if (sPlayerbotAIConfig.randomBotAutologin && sPlayerbotAIConfig.randomBotPrintStatsInterval &&
        time(nullptr) > (printStatsTimer + sPlayerbotAIConfig.randomBotPrintStatsInterval))
    {
        if (!printStatsTimer)
        {
            printStatsTimer = time(nullptr);
        }
        else
        {
            sRandomPlayerbotMgr.PrintStats();
            // activatePrintStatsThread();
        }
    }
    uint32 updateBots = sPlayerbotAIConfig.randomBotsPerInterval * onlineBotFocus / 100;
    uint32 maxNewBots =
        onlineBotCount < maxAllowedBotCount &&
                (sPlayerbotAIConfig.disabledWithoutRealPlayer == false ||
                 (realPlayerIsLogged && DelayLoginBotsTimer != 0 && time(nullptr) >= DelayLoginBotsTimer))
            ? maxAllowedBotCount - onlineBotCount
            : 0;
    uint32 loginBots = std::min(sPlayerbotAIConfig.randomBotsPerInterval - updateBots, maxNewBots);

    if (!availableBots.empty())
    {
        // Update bots
        for (auto bot : availableBots)
        {
            if (!GetPlayerBot(bot))
                continue;

            if (ProcessBot(bot))
            {
                updateBots--;
            }

            if (!updateBots)
                break;
        }

        if (loginBots && !HasPendingLogins())
        {
            loginBots += updateBots;
            loginBots = std::min(loginBots, maxNewBots);

            LOG_DEBUG("playerbots", "{} new bots prepared to login", loginBots);

            // Log in bots
            for (auto bot : availableBots)
            {
                if (GetPlayerBot(bot))
                    continue;

                if (ProcessBot(bot))
                {
                    loginBots--;
                }

                if (!loginBots)
                    break;
            }

            DelayLoginBotsTimer = 0;
        }
    }

    if (pmo)
        pmo->finish();

    if (sPlayerbotAIConfig.hasLog("player_location.csv"))
    {
        LogPlayerLocation();
    }
}

// void RandomPlayerbotMgr::ScaleBotActivity()
//{
//     float activityPercentage = getActivityPercentage();
//
//     // if (activityPercentage >= 100.0f || activityPercentage <= 0.0f) pid.reset(); //Stop integer buildup during
//     // max/min activity
//
//     //    % increase/decrease                   wanted diff                                         , avg diff
//     float activityPercentageMod = pid.calculate(
//         sRandomPlayerbotMgr.GetPlayers().empty() ? sPlayerbotAIConfig.diffEmpty :
//         sPlayerbotAIConfig.diffWithPlayer, sWorldUpdateTime.GetAverageUpdateTime());
//
//     activityPercentage = activityPercentageMod + 50;
//
//     // Cap the percentage between 0 and 100.
//     activityPercentage = std::max(0.0f, std::min(100.0f, activityPercentage));
//
//     setActivityPercentage(activityPercentage);
// }

// Assigns accounts as RNDbot accounts (type 1) based on MaxRandomBots and EnablePeriodicOnlineOffline and its ratio,
// and assigns accounts as AddClass accounts (type 2) based AddClassAccountPoolSize. Type 1 and 2 assignments are
// permenant, unless MaxRandomBots or AddClassAccountPoolSize are set to 0. If so, their associated accounts will
// be unassigned (type 0)
void RandomPlayerbotMgr::AssignAccountTypes()
{
    LOG_INFO("playerbots", "Assigning account types for random bot accounts...");

    // Clear existing filtered lists
    rndBotTypeAccounts.clear();
    addClassTypeAccounts.clear();

    // First, get ALL randombot accounts from the database
    std::vector<uint32> allRandomBotAccounts;
    QueryResult allAccounts = LoginDatabase.Query(
        "SELECT id FROM account WHERE username LIKE '{}%%' ORDER BY id",
        sPlayerbotAIConfig.randomBotAccountPrefix.c_str());

    if (allAccounts)
    {
        do
        {
            Field* fields = allAccounts->Fetch();
            uint32 accountId = fields[0].Get<uint32>();
            allRandomBotAccounts.push_back(accountId);
        } while (allAccounts->NextRow());
    }

    LOG_INFO("playerbots", "Found {} total randombot accounts in database", allRandomBotAccounts.size());

    // Check existing assignments
    QueryResult existingAssignments = PlayerbotsDatabase.Query("SELECT account_id, account_type FROM playerbots_account_type");
    std::map<uint32, uint8> currentAssignments;

    if (existingAssignments)
    {
        do
        {
            Field* fields = existingAssignments->Fetch();
            uint32 accountId = fields[0].Get<uint32>();
            uint8 accountType = fields[1].Get<uint8>();
            currentAssignments[accountId] = accountType;
        } while (existingAssignments->NextRow());
    }

    // Mark ALL randombot accounts as unassigned if not already assigned
    for (uint32 accountId : allRandomBotAccounts)
    {
        if (currentAssignments.find(accountId) == currentAssignments.end())
        {
            PlayerbotsDatabase.Execute("INSERT INTO playerbots_account_type (account_id, account_type) VALUES ({}, 0) ON DUPLICATE KEY UPDATE account_type = account_type", accountId);
            currentAssignments[accountId] = 0;
        }
    }

    // Calculate needed RNDbot accounts
    uint32 neededRndBotAccounts = 0;
    if (sPlayerbotAIConfig.maxRandomBots > 0)
    {
        int divisor = RandomPlayerbotFactory::CalculateAvailableCharsPerAccount();
        int maxBots = sPlayerbotAIConfig.maxRandomBots;

        // Take periodic online-offline into account
        if (sPlayerbotAIConfig.enablePeriodicOnlineOffline)
        {
            maxBots *= sPlayerbotAIConfig.periodicOnlineOfflineRatio;
        }

        // Calculate base accounts needed for RNDbots, ensuring round up for maxBots not cleanly divisible by the divisor
        neededRndBotAccounts = (maxBots + divisor - 1) / divisor;
    }

    // Count existing assigned accounts
    uint32 existingRndBotAccounts = 0;
    uint32 existingAddClassAccounts = 0;

    for (auto const& [accountId, accountType] : currentAssignments)
    {
        if (accountType == 1) existingRndBotAccounts++;
        else if (accountType == 2) existingAddClassAccounts++;
    }

    // Assign RNDbot accounts from lowest position if needed
    if (existingRndBotAccounts < neededRndBotAccounts)
    {
        uint32 toAssign = neededRndBotAccounts - existingRndBotAccounts;
        uint32 assigned = 0;

        for (uint32 i = 0; i < allRandomBotAccounts.size() && assigned < toAssign; i++)
        {
            uint32 accountId = allRandomBotAccounts[i];
            if (currentAssignments[accountId] == 0) // Unassigned
            {
                PlayerbotsDatabase.Execute("UPDATE playerbots_account_type SET account_type = 1, assignment_date = NOW() WHERE account_id = {}", accountId);
                currentAssignments[accountId] = 1;
                assigned++;
            }
        }

        if (assigned < toAssign)
        {
            LOG_ERROR("playerbots", "Not enough unassigned accounts to fulfill RNDbot requirements. Need {} more accounts.", toAssign - assigned);
        }
    }

    // Assign AddClass accounts from highest position if needed
    uint32 neededAddClassAccounts = sPlayerbotAIConfig.addClassAccountPoolSize;

    if (existingAddClassAccounts < neededAddClassAccounts)
    {
        uint32 toAssign = neededAddClassAccounts - existingAddClassAccounts;
        uint32 assigned = 0;

        for (size_t idx = allRandomBotAccounts.size(); idx-- > 0 && assigned < toAssign;)
        {
            uint32 accountId = allRandomBotAccounts[idx];
            if (currentAssignments[accountId] == 0) // Unassigned
            {
                PlayerbotsDatabase.Execute("UPDATE playerbots_account_type SET account_type = 2, assignment_date = NOW() WHERE account_id = {}", accountId);
                currentAssignments[accountId] = 2;
                assigned++;
            }
        }

        if (assigned < toAssign)
        {
            LOG_ERROR("playerbots", "Not enough unassigned accounts to fulfill AddClass requirements. Need {} more accounts.", toAssign - assigned);
        }
    }

    // Populate filtered account lists with ALL accounts of each type
    for (auto const& [accountId, accountType] : currentAssignments)
    {
        if (accountType == 1) rndBotTypeAccounts.push_back(accountId);
        else if (accountType == 2) addClassTypeAccounts.push_back(accountId);
    }

    LOG_INFO("playerbots", "Account type assignment complete: {} RNDbot accounts, {} AddClass accounts, {} unassigned",
             rndBotTypeAccounts.size(), addClassTypeAccounts.size(),
             currentAssignments.size() - rndBotTypeAccounts.size() - addClassTypeAccounts.size());
}

bool RandomPlayerbotMgr::IsAccountType(uint32 accountId, uint8 accountType)
{
    QueryResult result = PlayerbotsDatabase.Query("SELECT 1 FROM playerbots_account_type WHERE account_id = {} AND account_type = {}", accountId, accountType);
    return result != nullptr;
}

// Logs-in bots in 4 phases. Phase 1 logs Alliance bots up to how much is expected according to the faction ratio,
// and Phase 2 logs-in the remainder Horde bots to reach the total maxAllowedBotCount. If maxAllowedBotCount is not
// reached after Phase 2, the function goes back to log-in Alliance bots and reach maxAllowedBotCount. This is done
// because not every account is guaranteed 5A/5H bots, so the true ratio might be skewed by few percentages. Finally,
// Phase 4 is reached if and only if the value of RandomBotAccountCount is lower than it should.
uint32 RandomPlayerbotMgr::AddRandomBots()
{
    uint32 maxAllowedBotCount = GetTargetBotCount();
    static time_t missingBotsTimer = 0;

    if (currentBots.size() < maxAllowedBotCount)
    {
        // Calculate how many bots to add
        maxAllowedBotCount -= currentBots.size();
        maxAllowedBotCount = std::min(sPlayerbotAIConfig.randomBotsPerInterval, maxAllowedBotCount);

        // Single RNG instance for all shuffling
        std::mt19937 rng(std::chrono::steady_clock::now().time_since_epoch().count());

        // Only need to track the Alliance count, as it's in Phase 1
        uint32 totalRatio = sPlayerbotAIConfig.randomBotAllianceRatio + sPlayerbotAIConfig.randomBotHordeRatio;
        uint32 allowedAllianceCount = maxAllowedBotCount * (sPlayerbotAIConfig.randomBotAllianceRatio) / totalRatio;

        uint32 remainder = maxAllowedBotCount * (sPlayerbotAIConfig.randomBotAllianceRatio) % totalRatio;

        // Fix #1082: Randomly add one based on reminder
        if (remainder && urand(1, totalRatio) <= remainder)
        {
            allowedAllianceCount++;
        }

        // Determine which accounts to use based on EnablePeriodicOnlineOffline
        std::vector<uint32> accountsToUse;
        if (sPlayerbotAIConfig.enablePeriodicOnlineOffline)
        {

            // Calculate how many accounts can be used
            // With enablePeriodicOnlineOffline, don't use all of rndBotTypeAccounts right away. Fraction results are rounded up
            uint32 accountsToUseCount = (rndBotTypeAccounts.size() + sPlayerbotAIConfig.periodicOnlineOfflineRatio - 1)
                                        / sPlayerbotAIConfig.periodicOnlineOfflineRatio;

            // Randomly select accounts
            std::vector<uint32> shuffledAccounts = rndBotTypeAccounts;
            std::shuffle(shuffledAccounts.begin(), shuffledAccounts.end(), rng);

            for (uint32 i = 0; i < accountsToUseCount && i < shuffledAccounts.size(); i++)
            {
                accountsToUse.push_back(shuffledAccounts[i]);
            }
        }
        else
        {
            accountsToUse = rndBotTypeAccounts;
        }

        // Pre-map all characters from selected accounts
        struct CharacterInfo
        {
            uint32 guid;
            uint8 rClass;
            uint8 rRace;
            uint32 accountId;
        };
        std::vector<CharacterInfo> allCharacters;

        // One bounded roster read instead of a synchronous round trip for every bot account on every batch.
        // Filter the range back to the exact selected account set, including periodic online/offline rotation.
        if (!accountsToUse.empty())
        {
            auto const [minimum, maximum] = std::minmax_element(accountsToUse.begin(), accountsToUse.end());
            std::unordered_set<uint32> selectedAccounts(accountsToUse.begin(), accountsToUse.end());
            // A plain query: the core has no prepared statement for this range (Seth's patch added one to it).
            QueryResult result = CharacterDatabase.Query(
                "SELECT guid, class, race, account FROM characters WHERE account BETWEEN {} AND {}", *minimum, *maximum);
            if (result)
            {
                do
                {
                    Field* fields = result->Fetch();
                    uint32 accountId = fields[3].Get<uint32>();
                    if (!selectedAccounts.contains(accountId))
                        continue;
                    allCharacters.push_back({fields[0].Get<uint32>(), fields[1].Get<uint8>(),
                        fields[2].Get<uint8>(), accountId});
                } while (result->NextRow());
            }
        }

        // Shuffle for class balance
        std::shuffle(allCharacters.begin(), allCharacters.end(), rng);

        // Separate characters by faction for phased login
        std::vector<CharacterInfo> allianceChars;
        std::vector<CharacterInfo> hordeChars;

        for (auto const& charInfo : allCharacters)
        {
            if (IsAlliance(charInfo.rRace))
                allianceChars.push_back(charInfo);

            else
                hordeChars.push_back(charInfo);
        }

        // Lambda to handle bot login logic
        auto tryLoginBot = [&](CharacterInfo const& charInfo) -> bool
        {
            if (GetEventValue(charInfo.guid, "add") ||
                GetEventValue(charInfo.guid, "logout") ||
                GetPlayerBot(charInfo.guid) ||
                currentBots.contains(charInfo.guid) ||
                (sPlayerbotAIConfig.disableDeathKnightLogin && charInfo.rClass == CLASS_DEATH_KNIGHT))
            {
                return false;
            }

            uint32 add_time = sPlayerbotAIConfig.enablePeriodicOnlineOffline
                                ? urand(sPlayerbotAIConfig.minRandomBotInWorldTime,
                                        sPlayerbotAIConfig.maxRandomBotInWorldTime)
                                : sPlayerbotAIConfig.permanentlyInWorldTime;

            SetEventValue(charInfo.guid, "add", 1, add_time);
            SetEventValue(charInfo.guid, "logout", 0, 0);
            currentBots.insert(charInfo.guid);

            return true;
        };

        // PHASE 1: Log-in Alliance bots up to allowedAllianceCount
        for (auto const& charInfo : allianceChars)
        {
            if (!allowedAllianceCount)
                break;

            if (tryLoginBot(charInfo))
            {
                maxAllowedBotCount--;
                allowedAllianceCount--;
            }
        }

        // PHASE 2: Log-in Horde bots up to maxAllowedBotCount
        for (auto const& charInfo : hordeChars)
        {
            if (!maxAllowedBotCount)
                break;

            if (tryLoginBot(charInfo))
                maxAllowedBotCount--;
        }

        // PHASE 3: If maxAllowedBotCount wasn't reached, log-in more Alliance bots
        for (auto const& charInfo : allianceChars)
        {
            if (!maxAllowedBotCount)
                break;

            if (tryLoginBot(charInfo))
                maxAllowedBotCount--;
        }

        // PHASE 4: An error is given if maxAllowedBotCount is still not reached
        if (maxAllowedBotCount)
        {
            if (missingBotsTimer == 0)
                missingBotsTimer = time(nullptr);

            if (time(nullptr) - missingBotsTimer >= 10)
            {
                int divisor = RandomPlayerbotFactory::CalculateAvailableCharsPerAccount();
                uint32 moreAccountsNeeded = (maxAllowedBotCount + divisor - 1) / divisor;
                LOG_ERROR("playerbots",
                          "Can't log-in all the requested bots. Try increasing RandomBotAccountCount in your conf file.\n"
                          "{} more accounts needed.", moreAccountsNeeded);
                missingBotsTimer = 0;    // Reset timer so error is not spammed every tick
            }
        }
        else
        {
            missingBotsTimer = 0;       // Reset timer if logins for this interval were successful
        }
    }
    else
    {
        missingBotsTimer = 0;           // Reset timer if there's enough bots
    }

    return currentBots.size();
}

void RandomPlayerbotMgr::LoadBattleMastersCache()
{
    BattleMastersCache.clear();
    BattleMasterSpawnIds.clear();

    LOG_INFO("playerbots", "Loading Battlemasters Cache...");

    QueryResult result = WorldDatabase.Query("SELECT `entry`,`bg_template` FROM `battlemaster_entry`");

    if (!result)
    {
        return;
    }

    std::vector<std::pair<uint32, uint32>> battleMasterEntries;
    do
    {
        Field* fields = result->Fetch();
        uint32 entry = fields[0].Get<uint32>();
        battleMasterEntries.emplace_back(entry, fields[1].Get<uint32>());
        BattleMasterSpawnIds.emplace(entry, 0);
    } while (result->NextRow());

    // Resolve every entry to its first spawn once; 0 marks entries with no static spawn.
    for (auto const& [spawnId, creatureData] : sObjectMgr->GetAllCreatureData())
    {
        auto spawnIt = BattleMasterSpawnIds.find(creatureData.id);
        if (spawnIt != BattleMasterSpawnIds.end() && !spawnIt->second)
            spawnIt->second = spawnId;
    }

    uint32 count = 0;

    for (auto const& [entry, bgTypeId] : battleMasterEntries)
    {
        ++count;

        CreatureTemplate const* bmaster = sObjectMgr->GetCreatureTemplate(entry);
        if (!bmaster)
            continue;

        FactionTemplateEntry const* bmFaction = sFactionTemplateStore.LookupEntry(bmaster->faction);
        uint32 bmFactionId = bmFaction->faction;
        FactionEntry const* bmParentFaction = sFactionStore.LookupEntry(bmFactionId);
        uint32 bmParentTeam = bmParentFaction->team;
        TeamId bmTeam = TEAM_NEUTRAL;
        if (bmParentTeam == 891)
            bmTeam = TEAM_ALLIANCE;

        if (bmFactionId == 189)
            bmTeam = TEAM_ALLIANCE;

        if (bmParentTeam == 892)
            bmTeam = TEAM_HORDE;

        if (bmFactionId == 66)
            bmTeam = TEAM_HORDE;

        BattleMastersCache[bmTeam][BattlegroundTypeId(bgTypeId)].insert(
            BattleMastersCache[bmTeam][BattlegroundTypeId(bgTypeId)].end(), entry);
        LOG_DEBUG("playerbots", "Cached Battlemaster #{} for BG Type {} ({})", entry, bgTypeId,
                  bmTeam == TEAM_ALLIANCE ? "Alliance"
                  : bmTeam == TEAM_HORDE  ? "Horde"
                                          : "Neutral");
    }

    LOG_INFO("playerbots", ">> Loaded {} battlemaster entries", count);
}

std::vector<uint32> parseBrackets(std::string const& str)
{
    std::vector<uint32> brackets;
    std::stringstream ss(str);
    std::string item;

    while (std::getline(ss, item, ','))
    {
        brackets.push_back(static_cast<uint32>(std::stoi(item)));
    }

    return brackets;
}

// A rated solo queue entry (AscensionArenaQueue) is rated and holds one player: no rated team queues alone.
// Its 2v2 and 3v3 matches are played by two temporary teams, its 1v1 matches by personal teams. A party joined
// as a group with personal teams waits there as one entry too: the core files it with the solos, in the normal
// queues, where a rated arena team never goes.
enum class RatedSolo : uint8
{
    None,
    Waiting,
    Premade,
    Taken  // invited or in its match
};

static RatedSolo GetRatedSolo(Player* player, BattlegroundQueueTypeId queueTypeId, GroupQueueInfo* queued = nullptr)
{
    GroupQueueInfo ginfo;
    if (sBattlegroundMgr->GetBattlegroundQueue(queueTypeId).GetPlayerGroupInfoData(player->GetGUID(), &ginfo))
    {
        bool const premade = ginfo.Players.size() > 1 &&
                             (ginfo.GroupType == BG_QUEUE_NORMAL_ALLIANCE || ginfo.GroupType == BG_QUEUE_NORMAL_HORDE);
        if (!ginfo.IsRated || (ginfo.Players.size() != 1 && !premade))
            return RatedSolo::None;
        if (ginfo.IsInvitedToBGInstanceGUID)
            return RatedSolo::Taken;
        if (queued)
            *queued = ginfo;
        return premade ? RatedSolo::Premade : RatedSolo::Waiting;
    }

    Battleground* bg = player->GetBattleground();
    if (bg && bg->isArena() && bg->isRated() &&
        (bg->GetArenaType() == ARENA_TYPE_1v1 || bg->GetArenaTeamIdForTeam(TEAM_ALLIANCE) >= MAX_ARENA_TEAM_ID))
        return RatedSolo::Taken;
    return RatedSolo::None;
}

// A random bot leaves a battleground queue that will not start: 2 minutes after the last real player of that queue
// left it, or after 10 minutes in any case. Bots had filled a queue to 26/20 for a duo who then left it; the battle
// never started and the next players of that bracket waited behind them. Queues the bots fill on their own
// (RandomBotAutoJoinBG) keep them while no player is queued.
static bool LeavesStuckBgQueue(Player* bot, BattlegroundQueueTypeId queueTypeId, BattlegroundInfo const& info)
{
    if (bot->InBattleground() || bot->GetGroup())
        return false;
    BattlegroundQueue& queue = sBattlegroundMgr->GetBattlegroundQueue(queueTypeId);
    GroupQueueInfo ginfo;
    if (!queue.GetPlayerGroupInfoData(bot->GetGUID(), &ginfo) || ginfo.IsInvitedToBGInstanceGUID)
        return false;
    uint32 const waited = getMSTimeDiff(ginfo.JoinTime, GameTime::GetGameTimeMS().count());
    bool const noPlayer = !sPlayerbotAIConfig.randomBotAutoJoinBG && !(info.bgAlliancePlayerCount + info.bgHordePlayerCount);
    if (waited < (noPlayer ? 2 : 10) * MINUTE * IN_MILLISECONDS)
        return false;
    bot->RemoveBattlegroundQueueId(queueTypeId);
    queue.RemovePlayer(bot->GetGUID(), true);
    LOG_DEBUG("playerbots", "Bot {} left battleground queue {} after {} s ({})", bot->GetName(), uint32(queueTypeId),
              waited / IN_MILLISECONDS, noPlayer ? "no player queued" : "never started");
    return true;
}

void RandomPlayerbotMgr::CheckBgQueue()
{
    if (!BgCheckTimer)
    {
        BgCheckTimer = time(nullptr);
        return;  // Exit immediately after initializing the timer
    }

    if (time(nullptr) < BgCheckTimer)
    {
        return;  // No need to proceed if the current time is less than the timer
    }

    // Update the timer to the current time
    BgCheckTimer = time(nullptr);

    LOG_DEBUG("playerbots", "Checking BG Queue...");

    // Initialize Battleground Data (do not clear here)

    for (int bracket = BG_BRACKET_ID_FIRST; bracket < MAX_BATTLEGROUND_BRACKETS; ++bracket)
    {
        for (int queueType = BATTLEGROUND_QUEUE_AV; queueType < MAX_BATTLEGROUND_QUEUE_TYPES; ++queueType)
        {
            BattlegroundData[queueType][bracket] = BattlegroundInfo();
        }
    }

    ratedSoloWaiting.clear();
    ratedSoloPremades.clear();

    // Process real players and populate Battleground Data with player/queue count
    // Opens a queue for bots to join
    for (Player* player : players)
    {
        // Skip player if not currently in a queue
        if (!player->InBattlegroundQueue())
            continue;

        Battleground* bg = player->GetBattleground();
        if (bg && bg->GetStatus() == STATUS_WAIT_LEAVE)
            continue;

        TeamId teamId = player->GetTeamId();

        for (uint8 queueType = 0; queueType < PLAYER_MAX_BATTLEGROUND_QUEUES; ++queueType)
        {
            BattlegroundQueueTypeId queueTypeId = player->GetBattlegroundQueueTypeId(queueType);
            if (queueTypeId == BATTLEGROUND_QUEUE_NONE)
                continue;

            // Check if real player is able to create/join this queue
            BattlegroundTypeId bgTypeId = sBattlegroundMgr->BGTemplateId(queueTypeId);
            uint32 mapId = sBattlegroundMgr->GetBattlegroundTemplate(bgTypeId)->GetMapId();
            PvPDifficultyEntry const* pvpDiff = GetBattlegroundBracketByLevel(mapId, player->GetLevel());
            if (!pvpDiff)
                continue;

            // If player is allowed, populate the BattlegroundData with the appropriate level requirements
            BattlegroundBracketId bracketId = pvpDiff->GetBracketId();
            BattlegroundData[queueTypeId][bracketId].minLevel = pvpDiff->minLevel;
            BattlegroundData[queueTypeId][bracketId].maxLevel = pvpDiff->maxLevel;

            // Arena logic
            bool isRated = false;
            if (BattlegroundMgr::BGArenaType(queueTypeId))
            {
                // A rated solo player opens no queue for the rated teams of bots: they would wait for nothing.
                GroupQueueInfo queued;
                if (RatedSolo solo = GetRatedSolo(player, queueTypeId, &queued); solo != RatedSolo::None)
                {
                    BattlegroundInfo& info = BattlegroundData[queueTypeId][bracketId];
                    if (solo == RatedSolo::Waiting)
                    {
                        bool const healer = GetCoaRole(player) == CoaRole::Heal;
                        ++(healer ? info.ratedSoloPlayerHealers : info.ratedSoloPlayerDamage);
                        ratedSoloWaiting.push_back(
                            { player->GetGUID(), queued.JoinTime, queueTypeId, bracketId, healer, false });
                    }
                    // A party once, whichever of its real members is seen; its role read from the specs.
                    else if (solo == RatedSolo::Premade && !ratedSoloPremades.count(queued.ArenaTeamId))
                    {
                        uint32 healers = 0;
                        for (ObjectGuid const& guid : queued.Players)
                            if (Player* member = ObjectAccessor::FindPlayer(guid))
                                healers += GetCoaRole(member) == CoaRole::Heal ? 1 : 0;
                        ratedSoloPremades[queued.ArenaTeamId] = { queueTypeId, bracketId,
                                                                  uint32(queued.Players.size()), healers };
                        ratedSoloWaiting.push_back(
                            { *queued.Players.begin(), queued.JoinTime, queueTypeId, bracketId, healers > 0, true });
                    }
                    continue;
                }

                BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(queueTypeId);
                GroupQueueInfo ginfo;

                if (bgQueue.GetPlayerGroupInfoData(player->GetGUID(), &ginfo))
                {
                    isRated = ginfo.IsRated;
                }

                if (bgQueue.IsPlayerInvitedToRatedArena(player->GetGUID()) ||
                    (player->InArena() && player->GetBattleground()->isRated()))
                    isRated = true;

                if (isRated)
                    BattlegroundData[queueTypeId][bracketId].ratedArenaPlayerCount++;
                else
                    BattlegroundData[queueTypeId][bracketId].skirmishArenaPlayerCount++;
            }
            // BG Logic
            else
            {
                if (teamId == TEAM_ALLIANCE)
                {
                    BattlegroundData[queueTypeId][bracketId].bgAlliancePlayerCount++;
                    BattlegroundData[queueTypeId][bracketId].bgAllianceLevelSum += player->GetLevel();
                }
                else
                {
                    BattlegroundData[queueTypeId][bracketId].bgHordePlayerCount++;
                    BattlegroundData[queueTypeId][bracketId].bgHordeLevelSum += player->GetLevel();
                }

                // If a player has joined the BG, update the instance count in BattlegroundData (for consistency)
                if (player->InBattleground())
                {
                    std::vector<uint32>* instanceIds = nullptr;
                    uint32 instanceId = player->GetBattleground()->GetInstanceID();

                    instanceIds = &BattlegroundData[queueTypeId][bracketId].bgInstances;
                    if (instanceIds &&
                        std::find(instanceIds->begin(), instanceIds->end(), instanceId) == instanceIds->end())
                        instanceIds->push_back(instanceId);

                    BattlegroundData[queueTypeId][bracketId].bgInstanceCount = instanceIds->size();
                }
            }

            if (!player->IsInvitedForBattlegroundInstance() && !player->InBattleground())
            {
                if (BattlegroundMgr::BGArenaType(queueTypeId))
                {
                    if (isRated)
                        BattlegroundData[queueTypeId][bracketId].activeRatedArenaQueue = 1;
                    else
                        BattlegroundData[queueTypeId][bracketId].activeSkirmishArenaQueue = 1;
                }
                else
                {
                    BattlegroundData[queueTypeId][bracketId].activeBgQueue = 1;
                }
            }
        }
    }

    // A real player's battleground starts only once each faction holds its minimum in the core queue:
    // "21/20" was announced for a random battleground that never started, the 21 almost all of one
    // faction (jealous-sound #5826). What each faction still lacks is read from the queue itself.
    for (int bracket = BG_BRACKET_ID_FIRST; bracket < MAX_BATTLEGROUND_BRACKETS; ++bracket)
    {
        for (int queueType = BATTLEGROUND_QUEUE_AV; queueType < MAX_BATTLEGROUND_QUEUE_TYPES; ++queueType)
        {
            BattlegroundInfo& info = BattlegroundData[queueType][bracket];
            if (!info.activeBgQueue || !(info.bgAlliancePlayerCount + info.bgHordePlayerCount) ||
                BattlegroundMgr::BGArenaType(BattlegroundQueueTypeId(queueType)))
                continue;

            Battleground* bg =
                sBattlegroundMgr->GetBattlegroundTemplate(BattlegroundMgr::BGTemplateId(BattlegroundQueueTypeId(queueType)));
            PvPDifficultyEntry const* bracketEntry =
                bg ? GetBattlegroundBracketById(bg->GetMapId(), BattlegroundBracketId(bracket)) : nullptr;
            if (!bracketEntry)
                continue;

            uint32 const needed = GetMinPlayersPerTeam(bg, bracketEntry);
            BattlegroundQueue& queue = sBattlegroundMgr->GetBattlegroundQueue(BattlegroundQueueTypeId(queueType));
            uint32 const alliance = queue.GetPlayersCountInGroupsQueue(BattlegroundBracketId(bracket), BG_QUEUE_NORMAL_ALLIANCE);
            uint32 const horde = queue.GetPlayersCountInGroupsQueue(BattlegroundBracketId(bracket), BG_QUEUE_NORMAL_HORDE);
            info.bgAllianceMissing = needed > alliance ? needed - alliance : 0;
            info.bgHordeMissing = needed > horde ? needed - horde : 0;
            if (info.bgAllianceMissing || info.bgHordeMissing)
                LOG_DEBUG("playerbots", "BG queue {} bracket {}: {} Alliance and {} Horde queued, {} each needed",
                          queueType, bracket, alliance, horde, needed);
        }
    }

    // Process player bots
    for (auto& [guid, bot] : playerBots)
    {
        if (!bot || !bot->InBattlegroundQueue() || !bot->IsInWorld() || !IsRandomBot(bot))
            continue;

        Battleground* bg = bot->GetBattleground();
        if (bg && bg->GetStatus() == STATUS_WAIT_LEAVE)
            continue;

        TeamId teamId = bot->GetTeamId();

        for (uint8 queueType = 0; queueType < PLAYER_MAX_BATTLEGROUND_QUEUES; ++queueType)
        {
            BattlegroundQueueTypeId queueTypeId = bot->GetBattlegroundQueueTypeId(queueType);
            if (queueTypeId == BATTLEGROUND_QUEUE_NONE)
                continue;

            BattlegroundTypeId bgTypeId = sBattlegroundMgr->BGTemplateId(queueTypeId);
            uint32 mapId = sBattlegroundMgr->GetBattlegroundTemplate(bgTypeId)->GetMapId();
            PvPDifficultyEntry const* pvpDiff = GetBattlegroundBracketByLevel(mapId, bot->GetLevel());
            if (!pvpDiff)
                continue;

            BattlegroundBracketId bracketId = pvpDiff->GetBracketId();
            BattlegroundData[queueTypeId][bracketId].minLevel = pvpDiff->minLevel;
            BattlegroundData[queueTypeId][bracketId].maxLevel = pvpDiff->maxLevel;

            if (BattlegroundMgr::BGArenaType(queueTypeId))
            {
                // Bots of the rated solo queues are counted apart, their matches with them.
                if (RatedSolo solo = GetRatedSolo(bot, queueTypeId); solo != RatedSolo::None)
                {
                    BattlegroundInfo& info = BattlegroundData[queueTypeId][bracketId];
                    // A bot in the party of a real player is counted with that party.
                    if (solo == RatedSolo::Waiting)
                        ++(GetCoaRole(bot) == CoaRole::Heal ? info.ratedSoloBotHealers : info.ratedSoloBotDamage);
                    continue;
                }

                bool isRated = false;
                BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(queueTypeId);
                GroupQueueInfo ginfo;

                if (bgQueue.GetPlayerGroupInfoData(guid, &ginfo))
                {
                    isRated = ginfo.IsRated;
                }

                if (bgQueue.IsPlayerInvitedToRatedArena(guid) || (bot->InArena() && bot->GetBattleground()->isRated()))
                    isRated = true;

                if (isRated)
                    BattlegroundData[queueTypeId][bracketId].ratedArenaBotCount++;
                else
                    BattlegroundData[queueTypeId][bracketId].skirmishArenaBotCount++;
            }
            else
            {
                if (LeavesStuckBgQueue(bot, queueTypeId, BattlegroundData[queueTypeId][bracketId]))
                    continue;
                if (teamId == TEAM_ALLIANCE)
                {
                    BattlegroundData[queueTypeId][bracketId].bgAllianceBotCount++;
                    BattlegroundData[queueTypeId][bracketId].bgAllianceLevelSum += bot->GetLevel();
                }
                else
                {
                    BattlegroundData[queueTypeId][bracketId].bgHordeBotCount++;
                    BattlegroundData[queueTypeId][bracketId].bgHordeLevelSum += bot->GetLevel();
                }
            }

            if (bot->InBattleground())
            {
                std::vector<uint32>* instanceIds = nullptr;
                uint32 instanceId = bot->GetBattleground()->GetInstanceID();
                bool isArena = false;
                bool isRated = false;

                // Arena logic
                if (bot->InArena())
                {
                    isArena = true;
                    if (bot->GetBattleground()->isRated())
                    {
                        isRated = true;
                        instanceIds = &BattlegroundData[queueTypeId][bracketId].ratedArenaInstances;
                    }
                    else
                    {
                        instanceIds = &BattlegroundData[queueTypeId][bracketId].skirmishArenaInstances;
                    }
                }
                // BG Logic
                else
                {
                    instanceIds = &BattlegroundData[queueTypeId][bracketId].bgInstances;
                }

                if (instanceIds &&
                    std::find(instanceIds->begin(), instanceIds->end(), instanceId) == instanceIds->end())
                    instanceIds->push_back(instanceId);

                if (isArena)
                {
                    if (isRated)
                        BattlegroundData[queueTypeId][bracketId].ratedArenaInstanceCount = instanceIds->size();
                    else
                        BattlegroundData[queueTypeId][bracketId].skirmishArenaInstanceCount = instanceIds->size();
                }
                else
                {
                    BattlegroundData[queueTypeId][bracketId].bgInstanceCount = instanceIds->size();
                }
            }
        }
    }

    if (sPlayerbotAIConfig.coaRatedSoloQueue)
        CountRatedSoloNeeds();

    // If enabled, wait for (nearly) all bots to have logged in before queueing for Arena's / BG's.
    // 95 %, not all: a few bots are always logging out and in again (rotation, a new bot count), and on a
    // CoA test realm with 1,000 bots no battleground started for 30 to 40 minutes of every hour while the
    // count sat at 975-999.
    if (sPlayerbotAIConfig.randomBotAutoJoinBG && playerBots.size() * 100 >= GetMaxAllowedBotCount() * 95)
    {
        uint32 randomBotAutoJoinArenaBracket = sPlayerbotAIConfig.randomBotAutoJoinArenaBracket;
        uint32 randomBotAutoJoinBGRatedArena2v2Count = sPlayerbotAIConfig.randomBotAutoJoinBGRatedArena2v2Count;
        uint32 randomBotAutoJoinBGRatedArena3v3Count = sPlayerbotAIConfig.randomBotAutoJoinBGRatedArena3v3Count;
        uint32 randomBotAutoJoinBGRatedArena5v5Count = sPlayerbotAIConfig.randomBotAutoJoinBGRatedArena5v5Count;

        uint32 randomBotAutoJoinBGICCount = sPlayerbotAIConfig.randomBotAutoJoinBGICCount;
        uint32 randomBotAutoJoinBGEYCount = sPlayerbotAIConfig.randomBotAutoJoinBGEYCount;
        uint32 randomBotAutoJoinBGAVCount = sPlayerbotAIConfig.randomBotAutoJoinBGAVCount;
        uint32 randomBotAutoJoinBGABCount = sPlayerbotAIConfig.randomBotAutoJoinBGABCount;
        uint32 randomBotAutoJoinBGWSCount = sPlayerbotAIConfig.randomBotAutoJoinBGWSCount;

        std::vector<uint32> icBrackets = parseBrackets(sPlayerbotAIConfig.randomBotAutoJoinICBrackets);
        std::vector<uint32> eyBrackets = parseBrackets(sPlayerbotAIConfig.randomBotAutoJoinEYBrackets);
        std::vector<uint32> avBrackets = parseBrackets(sPlayerbotAIConfig.randomBotAutoJoinAVBrackets);
        std::vector<uint32> abBrackets = parseBrackets(sPlayerbotAIConfig.randomBotAutoJoinABBrackets);
        std::vector<uint32> wsBrackets = parseBrackets(sPlayerbotAIConfig.randomBotAutoJoinWSBrackets);

        // Check both bgInstanceCount / bgInstances.size
        // to help counter against potentional inconsistencies
        auto updateRatedArenaInstanceCount = [&](uint32 queueType, uint32 bracket, uint32 minCount)
        {
            if (BattlegroundData[queueType][bracket].activeRatedArenaQueue == 0 &&
                BattlegroundData[queueType][bracket].ratedArenaInstanceCount < minCount &&
                BattlegroundData[queueType][bracket].ratedArenaInstances.size() < minCount)
                BattlegroundData[queueType][bracket].activeRatedArenaQueue = 1;
        };

        auto updateBGInstanceCount = [&](uint32 queueType, std::vector<uint32> brackets, uint32 minCount)
        {
            for (uint32 bracket : brackets)
            {
                if (BattlegroundData[queueType][bracket].activeBgQueue == 0 &&
                    BattlegroundData[queueType][bracket].bgInstanceCount < minCount &&
                    BattlegroundData[queueType][bracket].bgInstances.size() < minCount)
                    BattlegroundData[queueType][bracket].activeBgQueue = 1;
            }
        };

        // Update rated arena instance counts
        updateRatedArenaInstanceCount(BATTLEGROUND_QUEUE_2v2, randomBotAutoJoinArenaBracket,
                                      randomBotAutoJoinBGRatedArena2v2Count);
        updateRatedArenaInstanceCount(BATTLEGROUND_QUEUE_3v3, randomBotAutoJoinArenaBracket,
                                      randomBotAutoJoinBGRatedArena3v3Count);
        updateRatedArenaInstanceCount(BATTLEGROUND_QUEUE_5v5, randomBotAutoJoinArenaBracket,
                                      randomBotAutoJoinBGRatedArena5v5Count);

        // Update battleground instance counts
        updateBGInstanceCount(BATTLEGROUND_QUEUE_IC, icBrackets, randomBotAutoJoinBGICCount);
        updateBGInstanceCount(BATTLEGROUND_QUEUE_EY, eyBrackets, randomBotAutoJoinBGEYCount);
        updateBGInstanceCount(BATTLEGROUND_QUEUE_AV, avBrackets, randomBotAutoJoinBGAVCount);
        updateBGInstanceCount(BATTLEGROUND_QUEUE_AB, abBrackets, randomBotAutoJoinBGABCount);
        updateBGInstanceCount(BATTLEGROUND_QUEUE_WS, wsBrackets, randomBotAutoJoinBGWSCount);
    }

    LogBattlegroundInfo();
}

// Bots for the rated solo queues a real player waits in, just enough to complete matches. A 1v1 pairs any two.
// A 2v2 or 3v3 lineup (AscensionArenaQueue::ComposeLineup) holds either one healer and damage dealers per side,
// or damage dealers only, and never leaves a waiting healer the only one of a match: each pair of real healers
// gets damage dealers, the real damage dealers left over fill matches of their own.
//
// The role of a real player is read from their spec, the core keeping the one they queued with. One who waited
// AiPlayerbot.CoaRatedSoloRoleFallback seconds with bots and no match is given one bot of the other role, once
// per wait; the bots no lineup needs are counted as surplus, to leave after their usual wait. A party too: the
// core takes the role its members last queued with, or damage, so a party read as holding a healer by its specs
// may meet a healer bot it cannot play against (each side has a healer or neither does).
void RandomPlayerbotMgr::CountRatedSoloNeeds()
{
    std::unordered_map<ObjectGuid, std::pair<uint32, bool>> fallbacks;
    uint32 const now = GameTime::GetGameTimeMS().count();
    uint32 const fallbackAfter = sPlayerbotAIConfig.coaRatedSoloRoleFallback * IN_MILLISECONDS;
    for (RatedSoloWait const& wait : ratedSoloWaiting)
    {
        uint8 const arenaType = BattlegroundMgr::BGArenaType(BattlegroundQueueTypeId(wait.queueTypeId));
        if (arenaType == ARENA_TYPE_1v1)
            continue;

        BattlegroundInfo& info = BattlegroundData[wait.queueTypeId][wait.bracketId];
        auto const sent = ratedSoloFallbacks.find(wait.guid);
        if (sent != ratedSoloFallbacks.end() && sent->second.first == wait.joinTime)
            fallbacks[wait.guid] = sent->second;
        else if (fallbackAfter && info.ratedSoloBotHealers + info.ratedSoloBotDamage &&
                 getMSTimeDiff(wait.joinTime, now) > fallbackAfter)
        {
            fallbacks[wait.guid] = { wait.joinTime, !wait.healer };
            Player* waiting = ObjectAccessor::FindPlayer(wait.guid);
            LOG_INFO("playerbots", "Rated solo {}v{}: {}{} still waits, read as {}: one {} bot more", uint32(arenaType),
                     uint32(arenaType), wait.party ? "the party of " : "player ",
                     waiting ? waiting->GetName() : wait.guid.ToString(),
                     wait.party ? (wait.healer ? "with a healer" : "without a healer") : (wait.healer ? "healer" : "damage"),
                     wait.healer ? "damage" : "healer");
        }
        else
            continue;

        ++(fallbacks[wait.guid].second ? info.ratedSoloFallbackHealers : info.ratedSoloFallbackDamage);
    }
    ratedSoloFallbacks.swap(fallbacks);

    // A party plays on one side (AscensionArenaQueue::ComposeLineup): damage dealers complete it, and the other
    // side is a full team, with a healer when the party has one, as each side has a healer or neither does.
    for (auto const& [teamId, premade] : ratedSoloPremades)
    {
        BattlegroundInfo& info = BattlegroundData[premade.queueTypeId][premade.bracketId];
        uint8 const arenaType = BattlegroundMgr::BGArenaType(BattlegroundQueueTypeId(premade.queueTypeId));
        uint32 const teamSize = sBattlegroundMgr->isArenaTesting() ? 1 : arenaType;
        if (premade.players > teamSize)
            continue;

        uint32 const opposingHealers = premade.healers ? 1 : 0;
        ++info.ratedSoloPremades;
        info.ratedSoloPremadeHealerSlots += opposingHealers;
        info.ratedSoloPremadeDamageSlots += teamSize - premade.players + teamSize - opposingHealers;
    }

    for (int bracket = BG_BRACKET_ID_FIRST; bracket < MAX_BATTLEGROUND_BRACKETS; ++bracket)
    {
        for (BattlegroundQueueTypeId queueTypeId : { BATTLEGROUND_QUEUE_1v1, BATTLEGROUND_QUEUE_2v2, BATTLEGROUND_QUEUE_3v3 })
        {
            BattlegroundInfo& info = BattlegroundData[queueTypeId][bracket];
            uint32 const realHealers = info.ratedSoloPlayerHealers;
            uint32 const realDamage = info.ratedSoloPlayerDamage;

            uint8 const arenaType = BattlegroundMgr::BGArenaType(queueTypeId);
            if (arenaType == ARENA_TYPE_1v1)
            {
                uint32 const real = realHealers + realDamage;
                uint32 const bots = info.ratedSoloBotHealers + info.ratedSoloBotDamage;
                info.ratedSoloDamageNeeded = real ? (real + bots) % 2 : 0;
                info.ratedSoloDamageSurplus = bots - std::min(bots, real % 2);
            }
            else
            {
                // Real solos take the slots the parties leave first; the others play matches of solos.
                uint32 const premadeHealers = info.ratedSoloPremadeHealerSlots;
                uint32 const premadeDamage = info.ratedSoloPremadeDamageSlots;
                uint32 const soloHealers = realHealers - std::min(realHealers, premadeHealers);
                uint32 const soloDamage = realDamage - std::min(realDamage, premadeDamage);
                uint32 const teamSize = sBattlegroundMgr->isArenaTesting() ? 1 : arenaType;
                uint32 const healerMatches = (soloHealers + 1) / 2;
                uint32 const healerMatchDamage = healerMatches * (2 * teamSize - 2);
                uint32 const damageLeft = soloDamage > healerMatchDamage ? soloDamage - healerMatchDamage : 0;
                uint32 const damageMatches = (damageLeft + 2 * teamSize - 1) / (2 * teamSize);
                uint32 const healers = premadeHealers + 2 * healerMatches + info.ratedSoloFallbackHealers;
                uint32 const damage =
                    premadeDamage + healerMatchDamage + damageMatches * 2 * teamSize + info.ratedSoloFallbackDamage;
                uint32 const haveHealers = realHealers + info.ratedSoloBotHealers;
                uint32 const haveDamage = realDamage + info.ratedSoloBotDamage;
                info.ratedSoloHealersNeeded = healers > haveHealers ? healers - haveHealers : 0;
                info.ratedSoloDamageNeeded = damage > haveDamage ? damage - haveDamage : 0;
                uint32 const botHealers = healers > realHealers ? healers - realHealers : 0;
                uint32 const botDamage = damage > realDamage ? damage - realDamage : 0;
                info.ratedSoloHealersSurplus =
                    info.ratedSoloBotHealers > botHealers ? info.ratedSoloBotHealers - botHealers : 0;
                info.ratedSoloDamageSurplus = info.ratedSoloBotDamage > botDamage ? info.ratedSoloBotDamage - botDamage : 0;
            }

            // Where a real player waits: what the bots are counted on, when it changes and every 2 minutes.
            if (realHealers + realDamage + info.ratedSoloPremades)
            {
                std::string const line = Acore::StringFormat(
                    "Rated solo {}v{} {}-{}: players {} healers {} damage, {} parties; bots queued {} healers {} damage; "
                    "needed {} healers {} damage; surplus {} healers {} damage; extra for roles {} healers {} damage",
                    uint32(arenaType), uint32(arenaType), info.minLevel, info.maxLevel, realHealers, realDamage,
                    info.ratedSoloPremades, info.ratedSoloBotHealers, info.ratedSoloBotDamage,
                    info.ratedSoloHealersNeeded, info.ratedSoloDamageNeeded, info.ratedSoloHealersSurplus,
                    info.ratedSoloDamageSurplus, info.ratedSoloFallbackHealers, info.ratedSoloFallbackDamage);
                auto& logged = ratedSoloNeedsLogged[uint32(queueTypeId) * MAX_BATTLEGROUND_BRACKETS + bracket];
                time_t const at = time(nullptr);
                if (logged.second != line || at - logged.first >= 120)
                {
                    LOG_INFO("playerbots", "{}", line);
                    logged = { at, line };
                }
            }
        }
    }
}

void RandomPlayerbotMgr::LogBattlegroundInfo()
{
    for (auto const& queueTypePair : BattlegroundData)
    {
        uint8 queueType = queueTypePair.first;

        BattlegroundQueueTypeId queueTypeId = BattlegroundQueueTypeId(queueType);

        if (uint8 type = BattlegroundMgr::BGArenaType(queueTypeId))
        {
            for (auto const& bracketIdPair : queueTypePair.second)
            {
                auto& bgInfo = bracketIdPair.second;
                if (bgInfo.minLevel == 0)
                    continue;
                LOG_INFO("playerbots",
                         "ARENA:{} {}: Player (Skirmish:{}, Rated:{}) Bots (Skirmish:{}, Rated:{}) Total (Skirmish:{} "
                         "Rated:{}), Instances (Skirmish:{} Rated:{})",
                         type == ARENA_TYPE_2v2   ? "2v2"
                         : type == ARENA_TYPE_3v3 ? "3v3"
                                                  : "5v5",
                         std::to_string(bgInfo.minLevel) + "-" + std::to_string(bgInfo.maxLevel),
                         bgInfo.skirmishArenaPlayerCount, bgInfo.ratedArenaPlayerCount, bgInfo.skirmishArenaBotCount,
                         bgInfo.ratedArenaBotCount, bgInfo.skirmishArenaPlayerCount + bgInfo.skirmishArenaBotCount,
                         bgInfo.ratedArenaPlayerCount + bgInfo.ratedArenaBotCount, bgInfo.skirmishArenaInstanceCount,
                         bgInfo.ratedArenaInstanceCount);
            }
            continue;
        }

        BattlegroundTypeId bgTypeId = BattlegroundMgr::BGTemplateId(queueTypeId);
        std::string _bgType;
        switch (bgTypeId)
        {
            case BATTLEGROUND_AV:
                _bgType = "AV";
                break;
            case BATTLEGROUND_WS:
                _bgType = "WSG";
                break;
            case BATTLEGROUND_AB:
                _bgType = "AB";
                break;
            case BATTLEGROUND_EY:
                _bgType = "EotS";
                break;
            case BATTLEGROUND_RB:
                _bgType = "Random";
                break;
            case BATTLEGROUND_SA:
                _bgType = "SotA";
                break;
            case BATTLEGROUND_IC:
                _bgType = "IoC";
                break;
            default:
                _bgType = "Other";
                break;
        }

        for (auto const& bracketIdPair : queueTypePair.second)
        {
            auto& bgInfo = bracketIdPair.second;
            if (bgInfo.minLevel == 0)
                continue;

            LOG_INFO("playerbots",
                     "BG:{} {}: Player ({}:{}) Bot ({}:{}) Total (A:{} H:{}), Instances {}, Active Queue: {}", _bgType,
                     std::to_string(bgInfo.minLevel) + "-" + std::to_string(bgInfo.maxLevel),
                     bgInfo.bgAlliancePlayerCount, bgInfo.bgHordePlayerCount, bgInfo.bgAllianceBotCount,
                     bgInfo.bgHordeBotCount, bgInfo.bgAlliancePlayerCount + bgInfo.bgAllianceBotCount,
                     bgInfo.bgHordePlayerCount + bgInfo.bgHordeBotCount, bgInfo.bgInstanceCount, bgInfo.activeBgQueue);
        }
    }
    LOG_DEBUG("playerbots", "BG Queue check finished");
}

void RandomPlayerbotMgr::CheckLfgQueue()
{
    if (!LfgCheckTimer || time(nullptr) > (LfgCheckTimer + 30))
        LfgCheckTimer = time(nullptr);

    LOG_DEBUG("playerbots", "Checking LFG Queue...");

    // Clear LFG list
    LfgDungeons[TEAM_ALLIANCE].clear();
    LfgDungeons[TEAM_HORDE].clear();
    std::map<TeamId, std::map<uint32, std::pair<uint8, uint8>>> playerLevels;
    bool needTankOrHeal[2] = {false, false};
    uint32 partiesWithoutTank[2] = {0, 0};
    uint32 partiesWithoutHeal[2] = {0, 0};
    std::unordered_set<ObjectGuid> countedParties;

    for (std::vector<Player*>::iterator i = players.begin(); i != players.end(); ++i)
    {
        Player* player = *i;
        if (!player || !player->IsInWorld())
            continue;

        Group* group = player->GetGroup();
        ObjectGuid guid = group ? group->GetGUID() : player->GetGUID();

        lfg::LfgState gState = sLFGMgr->GetState(guid);
        if (gState != lfg::LFG_STATE_NONE && gState < lfg::LFG_STATE_DUNGEON)
        {
            lfg::LfgDungeonSet const& dList = sLFGMgr->GetSelectedDungeons(player->GetGUID());
            for (lfg::LfgDungeonSet::const_iterator itr = dList.begin(); itr != dList.end(); ++itr)
            {
                lfg::LFGDungeonData const* dungeon = sLFGMgr->GetLFGDungeon(*itr);
                if (!dungeon)
                    continue;

                LfgDungeons[player->GetTeamId()].push_back(dungeon->id);
                uint8 const level = player->GetLevel();
                auto [levels, added] = playerLevels[player->GetTeamId()].try_emplace(dungeon->id, level, level);
                if (!added)
                {
                    levels->second.first = std::min(levels->second.first, level);
                    levels->second.second = std::max(levels->second.second, level);
                }
            }

            if (!sPlayerbotAIConfig.coaLfgMissingRoles)
            {
                uint8 roles = sLFGMgr->GetRoles(player->GetGUID());
                if (!(roles & lfg::PLAYER_ROLE_TANK) || !(roles & lfg::PLAYER_ROLE_HEALER))
                    needTankOrHeal[player->GetTeamId()] = true;
                continue;
            }

            if (!countedParties.insert(guid).second)
                continue;

            uint8 partyRoles = 0;
            if (group)
            {
                for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                    if (Player* member = ref->GetSource())
                        partyRoles |= sLFGMgr->GetRoles(member->GetGUID());
            }
            else
                partyRoles = sLFGMgr->GetRoles(player->GetGUID());

            if (!(partyRoles & lfg::PLAYER_ROLE_TANK))
                ++partiesWithoutTank[player->GetTeamId()];
            if (!(partyRoles & lfg::PLAYER_ROLE_HEALER))
                ++partiesWithoutHeal[player->GetTeamId()];
        }
    }

    if (sPlayerbotAIConfig.coaLfgMissingRoles &&
        (partiesWithoutTank[TEAM_ALLIANCE] + partiesWithoutTank[TEAM_HORDE] + partiesWithoutHeal[TEAM_ALLIANCE] +
         partiesWithoutHeal[TEAM_HORDE]) > 0)
    {
        uint32 queuedTanks[2] = {0, 0};
        uint32 queuedHeals[2] = {0, 0};
        for (auto itr = GetPlayerBotsBegin(); itr != GetPlayerBotsEnd(); ++itr)
        {
            Player* bot = itr->second;
            if (!bot || !bot->IsInWorld() || bot->GetGroup() ||
                sLFGMgr->GetState(bot->GetGUID()) != lfg::LFG_STATE_QUEUED)
                continue;

            uint8 roles = sLFGMgr->GetRoles(bot->GetGUID());
            if (roles & lfg::PLAYER_ROLE_TANK)
                ++queuedTanks[bot->GetTeamId()];
            if (roles & lfg::PLAYER_ROLE_HEALER)
                ++queuedHeals[bot->GetTeamId()];
        }

        for (uint8 team : {TEAM_ALLIANCE, TEAM_HORDE})
            needTankOrHeal[team] =
                partiesWithoutTank[team] > queuedTanks[team] || partiesWithoutHeal[team] > queuedHeals[team];
    }

    LfgNeedTankOrHeal[TEAM_ALLIANCE] = needTankOrHeal[TEAM_ALLIANCE];
    LfgNeedTankOrHeal[TEAM_HORDE] = needTankOrHeal[TEAM_HORDE];

    {
        std::lock_guard<std::mutex> lock(lfgPlayerLevelsLock);
        lfgPlayerLevels.swap(playerLevels);
    }

    LOG_DEBUG("playerbots", "LFG Queue check finished");
}

bool RandomPlayerbotMgr::IsNearLfgPlayerLevels(TeamId team, uint32 dungeonId, uint32 level)
{
    // Seen: bots of level 18-19 joined a player of 13 in Ragefire Chasm through the random dungeon.
    constexpr uint32 LFG_PLAYER_LEVEL_SPREAD = 5;

    std::lock_guard<std::mutex> lock(lfgPlayerLevelsLock);
    auto const teamLevels = lfgPlayerLevels.find(team);
    if (teamLevels == lfgPlayerLevels.end())
        return true;

    auto const levels = teamLevels->second.find(dungeonId);
    if (levels == teamLevels->second.end())
        return true;

    return level + LFG_PLAYER_LEVEL_SPREAD >= levels->second.first &&
           level <= levels->second.second + LFG_PLAYER_LEVEL_SPREAD;
}

void RandomPlayerbotMgr::CheckPlayers()
{
    if (!PlayersCheckTimer || time(nullptr) > (PlayersCheckTimer + 60))
        PlayersCheckTimer = time(nullptr);

    LOG_INFO("playerbots", "Checking Players...");

    if (!playersLevel)
        playersLevel = sPlayerbotAIConfig.randombotStartingLevel;

    for (std::vector<Player*>::iterator i = players.begin(); i != players.end(); ++i)
    {
        Player* player = *i;

        if (player->IsGameMaster())
            continue;

        // if (player->GetSession()->GetSecurity() > SEC_PLAYER)
        //     continue;

        if (player->GetLevel() > playersLevel)
            playersLevel = player->GetLevel() + 3;
    }

    LOG_INFO("playerbots", "Max player level is {}, max bot level set to {}", playersLevel - 3, playersLevel);
}

void RandomPlayerbotMgr::ScheduleRandomize(uint32 bot, uint32 time) { SetEventValue(bot, "randomize", 1, time); }

void RandomPlayerbotMgr::ScheduleTeleport(uint32 bot, uint32 time)
{
    if (!time)
        time = 60 + urand(sPlayerbotAIConfig.randomBotUpdateInterval, sPlayerbotAIConfig.randomBotUpdateInterval * 3);

    SetEventValue(bot, "teleport", 1, time);
}

void RandomPlayerbotMgr::ScheduleChangeStrategy(uint32 bot, uint32 time)
{
    if (!time)
        time = urand(sPlayerbotAIConfig.minRandomBotChangeStrategyTime,
                     sPlayerbotAIConfig.maxRandomBotChangeStrategyTime);

    SetEventValue(bot, "change_strategy", 1, time);
}

bool RandomPlayerbotMgr::ProcessBot(uint32 bot)
{
    ObjectGuid botGUID = ObjectGuid::Create<HighGuid::Player>(bot);
    Player* player = GetPlayerBot(botGUID);
    PlayerbotAI* botAI = player ? GET_PLAYERBOT_AI(player) : nullptr;

    uint32 isValid = GetEventValue(bot, "add");
    if (!isValid)
    {
        if (!player || !player->GetGroup())
        {
            if (player)
                LOG_DEBUG("playerbots", "Bot #{} {}:{} <{}>: log out", bot, IsAlliance(player->getRace()) ? "A" : "H",
                          player->GetLevel(), player->GetName().c_str());
            else
                LOG_DEBUG("playerbots", "Bot #{}: log out", bot);

            SetEventValue(bot, "add", 0, 0);
            currentBots.erase(bot);

            if (player)
                LogoutPlayerBot(botGUID);
        }

        return false;
    }

    uint32 randomTime;
    if (!player)
    {
        AddPlayerBot(botGUID, 0);
        randomTime = urand(1, 2);

        uint32 randomBotUpdateInterval = _isBotInitializing ? 1 : sPlayerbotAIConfig.randomBotUpdateInterval;
        randomTime = urand(std::max(5, static_cast<int>(randomBotUpdateInterval * 0.5)),
                           std::max(12, static_cast<int>(randomBotUpdateInterval * 2)));
        SetEventValue(bot, "update", 1, randomTime);

        // do not randomize or teleport immediately after server start (prevent lagging)
        if (!GetEventValue(bot, "randomize"))
        {
            randomTime = urand(3, std::max(4, static_cast<int>(randomBotUpdateInterval * 0.4)));
            ScheduleRandomize(bot, randomTime);
        }
        if (!GetEventValue(bot, "teleport"))
        {
            randomTime = urand(std::max(7, static_cast<int>(randomBotUpdateInterval * 0.7)),
                               std::max(14, static_cast<int>(randomBotUpdateInterval * 1.4)));
            ScheduleTeleport(bot, randomTime);
        }

        return true;
    }

    if (!player->IsInWorld())
        return false;

    if (player->GetGroup() || player->HasUnitState(UNIT_STATE_IN_FLIGHT))
        return false;

    uint32 update = GetEventValue(bot, "update");
    if (!update)
    {
        if (botAI)
            botAI->GetAiObjectContext()->GetValue<bool>("random bot update")->Set(true);

        bool update = true;
        if (botAI)
        {
            // botAI->GetAiObjectContext()->GetValue<bool>("random bot update")->Set(true);
            if (!sRandomPlayerbotMgr.IsRandomBot(player))
                update = false;

            if (player->GetGroup() && botAI->GetGroupLeader())
            {
                PlayerbotAI* groupLeaderBotAI = GET_PLAYERBOT_AI(botAI->GetGroupLeader());
                if (!groupLeaderBotAI || IsSelfBot(botAI->GetGroupLeader()))
                    update = false;
            }

            // if (botAI->HasPlayerNearby(sPlayerbotAIConfig.grindDistance))
            //     update = false;
        }

        if (update)
            ProcessBot(player);

        randomTime = urand(sPlayerbotAIConfig.minRandomBotReviveTime, sPlayerbotAIConfig.maxRandomBotReviveTime);
        SetEventValue(bot, "update", 1, randomTime);

        return true;
    }

    uint32 logout = GetEventValue(bot, "logout");
    if (player && !logout && !isValid)
    {
        LOG_DEBUG("playerbots", "Bot #{} {}:{} <{}>: log out", bot, IsAlliance(player->getRace()) ? "A" : "H",
                  player->GetLevel(), player->GetName().c_str());
        LogoutPlayerBot(botGUID);
        currentBots.erase(bot);
        SetEventValue(bot, "logout", 1,
                      urand(sPlayerbotAIConfig.minRandomBotInWorldTime, sPlayerbotAIConfig.maxRandomBotInWorldTime));
        return true;
    }

    return false;
}

bool RandomPlayerbotMgr::ProcessBot(Player* bot)
{

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return false;

    if (bot->InBattleground())
        return false;

    if (bot->InBattlegroundQueue())
        return false;

    CoaApplyRuleset(bot);

     uint32 botId = bot->GetGUID().GetCounter();

    // if death revive
    if (bot->isDead())
    {
        if (!GetEventValue(botId, "dead"))
        {
            uint32 randomTime =
                urand(sPlayerbotAIConfig.minRandomBotReviveTime, sPlayerbotAIConfig.maxRandomBotReviveTime);
            LOG_DEBUG("playerbots", "Mark bot {} as dead, will be revived in {}s.", bot->GetName().c_str(),
                      randomTime);
            SetEventValue(botId, "dead", 1, sPlayerbotAIConfig.maxRandomBotInWorldTime);
            SetEventValue(botId, "revive", 1, randomTime);
            return false;
        }

        if (!GetEventValue(botId, "revive"))
        {
            Revive(bot);
            return true;
        }

        return false;
    }

    // leave group if leader is rndbot
    Group* group = bot->GetGroup();
    if (group && !group->isLFGGroup() && IsRandomBot(group->GetLeader()))
    {
        botAI->LeaveOrDisbandGroup();
        LOG_INFO("playerbots", "Bot {} remove from group since leader is random bot.", bot->GetName().c_str());
    }

    // CoA: on a new install every new bot starts at level 1 in the same few starting areas, and those waiting on
    // the respawn of a "kill 8" quest mob stood by the quest giver for good (Blood Elves at Deathknell, 07/10). A
    // low level bot on its own that earns no experience for 10 minutes goes to another grinding spot of its level.
    if (bot->GetLevel() <= 10 && !bot->GetGroup() && !bot->IsInCombat())
    {
        uint32 const progress = bot->GetLevel() * 1000000 + bot->GetUInt32Value(PLAYER_XP) + 1;
        uint32 const now = uint32(time(nullptr));
        if (GetEventValue(botId, "coaXp") != progress)
        {
            SetEventValue(botId, "coaXp", progress, 30 * DAY);
            SetEventValue(botId, "coaXpAt", now, 30 * DAY);
        }
        else if (now - GetEventValue(botId, "coaXpAt") >= 10 * MINUTE)
        {
            LOG_INFO("playerbots", "coa: {} (level {}) earned no experience for 10 minutes, new grinding spot",
                     bot->GetName(), bot->GetLevel());
            SetEventValue(botId, "coaXpAt", now, 30 * DAY);
            RandomTeleportGrindForLevel(bot);
            return true;
        }
    }

    // only randomize and teleport idle bots
    bool idleBot = false;
    if (TravelTarget* target = botAI->GetAiObjectContext()->GetValue<TravelTarget*>("travel target")->Get())
    {
        if (target->getTravelState() == TravelState::TRAVEL_STATE_IDLE)
        {
            idleBot = true;
        }
    }
    else
    {
        idleBot = true;
    }

    if (idleBot)
    {
        // randomize
        uint32 randomize = GetEventValue(botId, "randomize");
        if (!randomize)
        {
            // bool randomiser = true;
            // if (player->GetGuildId())
            // {
            //     if (Guild* guild = sGuildMgr->GetGuildById(player->GetGuildId()))
            //     {
            //         if (guild->GetLeaderGUID() == player->GetGUID())
            //         {
            //             for (std::vector<Player*>::iterator i = players.begin(); i != players.end(); ++i)
            //                 GuildTaskMgr::instance().Update(*i, player);
            //         }

            //         uint32 accountId = sCharacterCache->GetCharacterAccountIdByGuid(guild->GetLeaderGUID());
            //         if (!sPlayerbotAIConfig.IsInRandomAccountList(accountId))
            //         {
            //             uint8 rank = player->GetRank();
            //             randomiser = rank < 4 ? false : true;
            //         }
            //     }
            // }
            // if (randomiser)
            // {
            Randomize(bot);
            LOG_DEBUG("playerbots", "Bot #{} {}:{} <{}>: randomized", botId,
                      bot->GetTeamId() == TEAM_ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName());
            uint32 randomTime =
                urand(sPlayerbotAIConfig.minRandomBotRandomizeTime, sPlayerbotAIConfig.maxRandomBotRandomizeTime);
            ScheduleRandomize(botId, randomTime);
            return true;
        }

        // uint32 changeStrategy = GetEventValue(bot, "change_strategy");
        // if (!changeStrategy)
        // {
        //     LOG_INFO("playerbots", "Changing strategy for bot  #{} <{}>", bot, player->GetName().c_str());
        //     ChangeStrategy(player);
        //     return true;
        // }

        uint32 teleport = GetEventValue(botId, "teleport");
        if (!teleport)
        {
            LOG_DEBUG("playerbots", "Bot #{} <{}>: teleport for level and refresh", botId, bot->GetName());
            Refresh(bot);
            RandomTeleportForLevel(bot);
            uint32 time = urand(sPlayerbotAIConfig.minRandomBotTeleportInterval,
                                sPlayerbotAIConfig.maxRandomBotTeleportInterval);
            ScheduleTeleport(botId, time);
            return true;
        }
    }

    return false;
}

void RandomPlayerbotMgr::Revive(Player* player)
{
    uint32 bot = player->GetGUID().GetCounter();

    // LOG_INFO("playerbots", "Bot {} revived", player->GetName().c_str());
    SetEventValue(bot, "dead", 0, 0);
    SetEventValue(bot, "revive", 0, 0);

    Refresh(player);
    RandomTeleportGrindForLevel(player);
}

void RandomPlayerbotMgr::RandomTeleport(Player* bot, std::vector<WorldLocation>& locs, bool hearth)
{
    // ignore when alrdy teleported or not in the world yet.
    if (bot->IsBeingTeleported() || !bot->IsInWorld())
        return;

    // no teleport / movement update when rooted.
    if (bot->IsRooted())
        return;

    // ignore when in queue for battle grounds.
    if (bot->InBattlegroundQueue())
        return;

    // ignore when in battle grounds or arena.
    if (bot->InBattleground() || bot->InArena())
        return;

    // ignore when in group (e.g. world, dungeons, raids) and leader is not a player.
    if (bot->GetGroup() && !bot->GetGroup()->IsLeader(bot->GetGUID()))
        return;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (botAI)
    {
        // ignore when in when taxi with boat/zeppelin and has players nearby
        if (bot->HasUnitMovementFlag(MOVEMENTFLAG_ONTRANSPORT) && bot->HasUnitState(UNIT_STATE_IGNORE_PATHFINDING) &&
            botAI->HasPlayerNearby())
            return;
    }

    // if (sPlayerbotAIConfig.randomBotRpgChance < 0)
    //     return;

    if (locs.empty())
    {
        LOG_DEBUG("playerbots", "Cannot teleport bot {} - no locations available", bot->GetName().c_str());
        return;
    }

    std::vector<WorldPosition> tlocs;
    for (auto& loc : locs)
        tlocs.push_back(WorldPosition(loc));
    // Do not teleport to maps disabled in config
    tlocs.erase(std::remove_if(tlocs.begin(), tlocs.end(),
                               [](WorldPosition l)
                               {
                                   std::vector<uint32>::iterator i =
                                       find(sPlayerbotAIConfig.randomBotMaps.begin(),
                                            sPlayerbotAIConfig.randomBotMaps.end(), l.GetMapId());
                                   return i == sPlayerbotAIConfig.randomBotMaps.end();
                               }),
                tlocs.end());
    if (tlocs.empty())
    {
        LOG_DEBUG("playerbots", "Cannot teleport bot {} - all locations removed by filter", bot->GetName().c_str());
        return;
    }

    PerfMonitorOperation* pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "RandomTeleportByLocations");

    std::shuffle(std::begin(tlocs), std::end(tlocs), RandomEngine::Instance());
    for (uint32 i = 0; i < tlocs.size(); i++)
    {
        WorldLocation loc = tlocs[i];

        float x = loc.GetPositionX();  // + (attemtps > 0 ? urand(0, sPlayerbotAIConfig.grindDistance) -
                                       // sPlayerbotAIConfig.grindDistance / 2 : 0);
        float y = loc.GetPositionY();  // + (attemtps > 0 ? urand(0, sPlayerbotAIConfig.grindDistance) -
                                       // sPlayerbotAIConfig.grindDistance / 2 : 0);
        float z = loc.GetPositionZ();

        Map* map = sMapMgr->FindMap(loc.GetMapId(), 0);
        if (!map)
            continue;

        AreaTableEntry const* zone = sAreaTableStore.LookupEntry(map->GetZoneId(bot->GetPhaseMask(), x, y, z));
        if (!zone)
            continue;

        AreaTableEntry const* area = sAreaTableStore.LookupEntry(map->GetAreaId(bot->GetPhaseMask(), x, y, z));
        if (!area)
            continue;

        // Do not teleport to enemy zones if level is low
        if (zone->team == 4 && bot->GetTeamId() == TEAM_ALLIANCE)
            continue;

        if (zone->team == 2 && bot->GetTeamId() == TEAM_HORDE)
            continue;

        if (map->IsInWater(bot->GetPhaseMask(), x, y, z, bot->GetCollisionHeight()))
            continue;

        float ground = map->GetHeight(bot->GetPhaseMask(), x, y, z + 0.5f);
        if (ground <= INVALID_HEIGHT)
            continue;

        z = 0.05f + ground;

        if (!botAI->StarterLevelDistanceCheck(bot, loc, true))
            continue;

        LocaleConstant const& locale = sWorld->GetDefaultDbcLocale();
        LOG_DEBUG("playerbots",
                  "Random teleporting bot {} (level {}) to Map: {} ({}) Zone: {} ({}) Area: {} ({}) ZoneLevel: {} "
                  "AreaLevel: {} {},{},{} ({}/{} "
                  "locations)",
                  bot->GetName().c_str(), bot->GetLevel(), map->GetId(), map->GetMapName(), zone->ID,
                  zone->area_name[locale], area->ID, area->area_name[locale], zone->area_level, area->area_level, x, y,
                  z, i + 1, tlocs.size());

        if (hearth)
        {
            bot->SetHomebind(loc, zone->ID);
        }

        // Prevent blink to be detected by visible real players
        if (botAI->HasPlayerNearby(150.0f))
        {
            break;
        }

        bot->GetMotionMaster()->Clear();
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (botAI)
            botAI->Reset(true);
        bot->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TELEPORTED | AURA_INTERRUPT_FLAG_CHANGE_MAP);
        bot->TeleportTo(loc.GetMapId(), x, y, z, 0);
        bot->SendMovementFlagUpdate();

        if (pmo)
            pmo->finish();

        return;
    }

    if (pmo)
        pmo->finish();

    // LOG_ERROR("playerbots", "Cannot teleport bot {} - no locations available ({} locations)", bot->GetName().c_str(),
    //           tlocs.size());
}

void RandomPlayerbotMgr::PrepareAddclassCache()
{
    // Using accounts marked as type 2 (AddClass)
    int32 collected = 0;

    for (uint32 accountId : addClassTypeAccounts)
    {
        for (uint8 claz = CLASS_WARRIOR; claz <= CLASS_DRUID; claz++)
        {
            if (claz == 10)
                continue;

            QueryResult results = CharacterDatabase.Query(
                "SELECT guid, race FROM characters "
                "WHERE account = {} AND class = '{}' AND online = 0",
                accountId, claz);

            if (results)
            {
                do
                {
                    Field* fields = results->Fetch();
                    ObjectGuid guid = ObjectGuid(HighGuid::Player, fields[0].Get<uint32>());
                    uint32 race = fields[1].Get<uint32>();
                    bool isAlliance = race == 1 || race == 3 || race == 4 || race == 7 || race == 11;
                    addclassCache[GetTeamClassIdx(isAlliance, claz)].insert(guid);
                    collected++;
                } while (results->NextRow());
            }
        }
    }

    LOG_INFO("playerbots", ">> {} characters collected for addclass command from {} AddClass accounts.", collected, addClassTypeAccounts.size());
}

void RandomPlayerbotMgr::Init()
{
    if (sPlayerbotAIConfig.addClassCommand)
        sRandomPlayerbotMgr.PrepareAddclassCache();

    if (sPlayerbotAIConfig.randomBotJoinBG)
        sRandomPlayerbotMgr.LoadBattleMastersCache();

    PlayerbotsDatabase.Execute("DELETE FROM playerbots_random_bots WHERE event = 'add'");
}

// CoA: a bot team queues only when its captain is online, and every restart or rotation logs in other bots, so
// the teams stopped playing. Their captains are kept in the world here; they log in their team when they queue.
// They are logged in at once, above the bot count, like the team members the captain calls: a captain only made
// a random bot would wait for a free place, more than an hour on a full world.
void RandomPlayerbotMgr::KeepArenaCaptainsOnline()
{
    ArenaCaptainCheckTimer = time(nullptr);
    if (sPlayerbotAIConfig.deleteRandomBotArenaTeams)
        return;

    std::pair<ArenaType, uint32> const formats[] = {
        {ARENA_TYPE_2v2, sPlayerbotAIConfig.randomBotAutoJoinBGRatedArena2v2Count},
        {ARENA_TYPE_3v3, sPlayerbotAIConfig.randomBotAutoJoinBGRatedArena3v3Count},
        {ARENA_TYPE_5v5, sPlayerbotAIConfig.randomBotAutoJoinBGRatedArena5v5Count}};

    uint32 logins = 0;
    for (auto const& [type, count] : formats)
    {
        if (!count)
            continue;

        std::vector<ObjectGuid> captains;
        RandomPlayerbotFactory::GetBotArenaTeamCaptains(type, captains);
        for (ObjectGuid const& captain : captains)
        {
            uint32 const bot = captain.GetCounter();
            if (logins >= 10 || ObjectAccessor::FindConnectedPlayer(captain) ||
                !sPlayerbotAIConfig.IsInRandomAccountList(sCharacterCache->GetCharacterAccountIdByGuid(captain)))
                continue;

            if (!currentBots.contains(bot))
            {
                SetEventValue(bot, "add", 1, sPlayerbotAIConfig.permanentlyInWorldTime);
                SetEventValue(bot, "logout", 0, 0);
                currentBots.insert(bot);
            }

            AddPlayerBot(captain, 0);
            ++logins;
        }
    }

    if (logins)
        LOG_INFO("playerbots", "Arena captains brought back online: {}", logins);
}

void RandomPlayerbotMgr::InitArenaTeams()
{
    if (sPlayerbotAIConfig.deleteRandomBotArenaTeams)
    {
        RandomPlayerbotFactory::DeleteBotArenaTeams();
        return;
    }

    RandomPlayerbotFactory::LoadArenaTeamData();

    LOG_INFO("playerbots", "Bot arena teams: 2v2={}/{}, 3v3={}/{}, 5v5={}/{}",
             RandomPlayerbotFactory::GetBotArenaTeamCount(ARENA_TYPE_2v2), sPlayerbotAIConfig.randomBotArenaTeam2v2Count,
             RandomPlayerbotFactory::GetBotArenaTeamCount(ARENA_TYPE_3v3), sPlayerbotAIConfig.randomBotArenaTeam3v3Count,
             RandomPlayerbotFactory::GetBotArenaTeamCount(ARENA_TYPE_5v5), sPlayerbotAIConfig.randomBotArenaTeam5v5Count);
}

void RandomPlayerbotMgr::RandomTeleportForLevel(Player* bot)
{
    if (bot->InBattleground())
        return;

    if (bot->GetLevel() >= 10 && urand(0, 100) < sPlayerbotAIConfig.probTeleToBankers * 100)
    {
        std::vector<WorldLocation> locs = sTravelMgr.GetCityLocations(bot);
        if (!locs.empty())
        {
            RandomTeleport(bot, locs, true);
            return;
        }
    }
    std::vector<WorldLocation> locs = sTravelMgr.GetTeleportLocations(bot);

    if (sPlayerbotAIConfig.randomBotConcentrateInPlayerZone && !locs.empty())
    {
        std::vector<WorldLocation> playerZoneLocs = GetPlayerZoneTeleportLocations(locs, bot);
        if (!playerZoneLocs.empty())
            locs = std::move(playerZoneLocs);
    }

    if (!locs.empty())
    {
        RandomTeleport(bot, locs, false);
        return;
    }
}

// Returns the subset of teleport locations that lie in a zone currently occupied by a real
// (non-GM) player, so random bots can be gathered where players actually are. Returns an empty
// vector when no player is online or none of the locations match, letting the caller fall back
// to the default world-wide behaviour.
std::vector<WorldLocation> RandomPlayerbotMgr::GetPlayerZoneTeleportLocations(std::vector<WorldLocation> const& locs,
                                                                              Player* bot)
{
    std::set<uint32> playerMaps;
    std::set<std::pair<uint32, uint32>> playerMapZones;

    // players only ever holds real (non random bot) players and is maintained on login/logout, so
    // this is a pass over the online player list, not a world-wide scan.
    for (Player* player : players)
    {
        if (!player || !player->IsInWorld() || player->IsGameMaster())
            continue;

        Map* map = player->GetMap();
        if (!map)
            continue;

        // Instanceable maps (dungeons, raids, battlegrounds, arenas) are never valid targets: a
        // WorldLocation carries no instance id, so a bot would be sent to another instance of the
        // same map rather than to the player.
        if (map->Instanceable())
            continue;

        // Resolve the player zone the same way as the candidate locations below (unphased terrain),
        // so a player standing in a phased area still matches its underlying zone.
        uint32 zoneId = map->GetZoneId(PHASEMASK_NORMAL, player->GetPositionX(), player->GetPositionY(),
                                       player->GetPositionZ());
        playerMaps.insert(map->GetId());
        playerMapZones.insert(std::make_pair(map->GetId(), zoneId));
    }

    std::vector<WorldLocation> filtered;
    if (playerMapZones.empty())
        return filtered;

    for (WorldLocation const& loc : locs)
    {
        if (playerMaps.find(loc.GetMapId()) == playerMaps.end())
            continue;

        uint32 zoneId = sMapMgr->GetZoneId(PHASEMASK_NORMAL, loc);
        if (playerMapZones.find(std::make_pair(loc.GetMapId(), zoneId)) == playerMapZones.end())
            continue;

        // Skip enemy-faction zones, matching the check in RandomTeleport. This has to be done here:
        // the caller only falls back to normal teleporting when the filtered set is empty, so keeping
        // a hostile location would let the downstream team check drain the set and strand the bot.
        if (AreaTableEntry const* zone = sAreaTableStore.LookupEntry(zoneId))
        {
            if (zone->team == 4 && bot->GetTeamId() == TEAM_ALLIANCE)
                continue;

            if (zone->team == 2 && bot->GetTeamId() == TEAM_HORDE)
                continue;
        }

        filtered.push_back(loc);
    }

    return filtered;
}

void RandomPlayerbotMgr::RandomTeleportGrindForLevel(Player* bot)
{
    if (bot->InBattleground())
        return;

    std::vector<WorldLocation> locs = sTravelMgr.GetTeleportLocations(bot);
    LOG_DEBUG("playerbots", "Random teleporting bot {} for level {} ({} locations available)", bot->GetName().c_str(),
              bot->GetLevel(), locs.size());

    RandomTeleport(bot, locs);
}

void RandomPlayerbotMgr::RandomTeleport(Player* bot)
{
    if (bot->InBattleground())
        return;

    PerfMonitorOperation* pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "RandomTeleport");
    std::vector<WorldLocation> locs;

    std::list<Unit*> targets;
    float range = sPlayerbotAIConfig.randomBotTeleportDistance;
    Acore::AnyUnitInObjectRangeCheck u_check(bot, range);
    Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(bot, targets, u_check);
    Cell::VisitObjects(bot, searcher, range);

    if (!targets.empty())
    {
        for (Unit* unit : targets)
        {
            bot->UpdatePosition(*unit);
            FleeManager manager(bot, sPlayerbotAIConfig.sightDistance, 0, true);
            float rx, ry, rz;
            if (manager.CalculateDestination(&rx, &ry, &rz))
            {
                WorldLocation loc(bot->GetMapId(), rx, ry, rz);
                locs.push_back(loc);
            }
        }
    }
    else
    {
        RandomTeleportForLevel(bot);
    }

    if (pmo)
        pmo->finish();

    Refresh(bot);
}

void RandomPlayerbotMgr::Randomize(Player* bot)
{
    if (bot->InBattleground())
        return;

    // A bot below level 3 is rebuilt from scratch, which rolls its level again
    // inside the configured range. When that range tops out below 3 the roll can
    // only return the bot to where it started, so a level 1 population could never
    // grow: every login rerolled the levels the bots had earned. Leave those bots
    // alone and let the branch below keep the level they reached.
    if ((bot->GetLevel() < 3 && bot->GetLevel() < sPlayerbotAIConfig.randomBotMaxLevel)
        || (bot->GetLevel() < 56 && bot->getClass() == CLASS_DEATH_KNIGHT))
    {
        RandomizeFirst(bot);
    }
    else if (bot->GetLevel() < sPlayerbotAIConfig.randomBotMaxLevel || !sPlayerbotAIConfig.downgradeMaxLevelBot)
    {
        uint8 level = bot->GetLevel();
        PlayerbotFactory factory(bot, level);
        factory.Randomize(true);
        // IncreaseLevel(bot);
    }
    else
    {
        RandomizeFirst(bot);
    }
}

void RandomPlayerbotMgr::IncreaseLevel(Player* bot)
{
    uint32 maxLevel = sPlayerbotAIConfig.randomBotMaxLevel;
    if (maxLevel > sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
        maxLevel = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);

    PerfMonitorOperation* pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "IncreaseLevel");
    uint32 lastLevel = GetValue(bot, "level");
    uint8 level = bot->GetLevel() + 1;
    if (level > maxLevel)
    {
        level = maxLevel;
    }
    if (lastLevel != level)
    {
        PlayerbotFactory factory(bot, level);
        factory.Randomize(true);
    }

    if (pmo)
        pmo->finish();
}

void RandomPlayerbotMgr::RandomizeFirst(Player* bot)
{
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return;

    uint32 maxLevel = sPlayerbotAIConfig.randomBotMaxLevel;
    if (maxLevel > sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
        maxLevel = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);

    // if lvl sync is enabled, max level is limited by online players lvl
    if (sPlayerbotAIConfig.syncLevelWithPlayers)
        maxLevel = std::max(sPlayerbotAIConfig.randomBotMinLevel,
                            std::min(playersLevel, sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL)));

    uint32 minLevel = sPlayerbotAIConfig.randomBotMinLevel;
    if (bot->getClass() == CLASS_DEATH_KNIGHT)
    {
        maxLevel = std::max(maxLevel, sWorld->getIntConfig(CONFIG_START_HEROIC_PLAYER_LEVEL));
        minLevel = std::max(minLevel, sWorld->getIntConfig(CONFIG_START_HEROIC_PLAYER_LEVEL));
    }

    // urand below asserts max >= min.
    minLevel = std::min(minLevel, maxLevel);

    PerfMonitorOperation* pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "RandomizeFirst");

    uint32 level;

    if (sPlayerbotAIConfig.downgradeMaxLevelBot && bot->GetLevel() >= sPlayerbotAIConfig.randomBotMaxLevel)
    {
        if (bot->getClass() == CLASS_DEATH_KNIGHT)
        {
            level = sWorld->getIntConfig(CONFIG_START_HEROIC_PLAYER_LEVEL);
        }
        else
        {
            level = sPlayerbotAIConfig.randomBotMinLevel;
        }
    }
    else
    {
        uint32 roll = urand(1, 100);
        if (roll <= 100 * sPlayerbotAIConfig.randomBotMaxLevelChance)
        {
            level = maxLevel;
        }
        else if (roll <=
                 (100 * (sPlayerbotAIConfig.randomBotMaxLevelChance + sPlayerbotAIConfig.randomBotMinLevelChance)))
        {
            level = minLevel;
        }
        else
        {
            level = urand(minLevel, maxLevel);
        }
    }

    if (sPlayerbotAIConfig.disableRandomLevels)
    {
        level = bot->getClass() == CLASS_DEATH_KNIGHT ? std::max(sPlayerbotAIConfig.randombotStartingLevel,
                                                                 sWorld->getIntConfig(CONFIG_START_HEROIC_PLAYER_LEVEL))
                                                      : sPlayerbotAIConfig.randombotStartingLevel;
    }

    SetValue(bot, "level", level);
    PlayerbotFactory factory(bot, level);
    factory.Randomize(false);

    uint32 randomTime =
        urand(sPlayerbotAIConfig.minRandomBotRandomizeTime, sPlayerbotAIConfig.maxRandomBotRandomizeTime);
    uint32 inworldTime =
        urand(sPlayerbotAIConfig.minRandomBotInWorldTime, sPlayerbotAIConfig.maxRandomBotInWorldTime);

    PlayerbotsDatabasePreparedStatement* stmt = PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_UPD_RANDOM_BOTS);
    stmt->SetData(0, randomTime);
    stmt->SetData(1, "bot_delete");
    stmt->SetData(2, bot->GetGUID().GetCounter());
    PlayerbotsDatabase.Execute(stmt);

    stmt = PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_UPD_RANDOM_BOTS);
    stmt->SetData(0, inworldTime);
    stmt->SetData(1, "logout");
    stmt->SetData(2, bot->GetGUID().GetCounter());
    PlayerbotsDatabase.Execute(stmt);

    // teleport to a random inn for bot level
    botAI->Reset(true);

    if (bot->GetGroup())
        botAI->LeaveOrDisbandGroup();

    if (pmo)
        pmo->finish();

    RandomTeleportForLevel(bot);
}

void RandomPlayerbotMgr::RandomizeMin(Player* bot)
{
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return;

    PerfMonitorOperation* pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "RandomizeMin");
    uint32 level = sPlayerbotAIConfig.randomBotMinLevel;
    SetValue(bot, "level", level);
    PlayerbotFactory factory(bot, level);
    factory.Randomize(false);

    uint32 randomTime =
        urand(sPlayerbotAIConfig.minRandomBotRandomizeTime, sPlayerbotAIConfig.maxRandomBotRandomizeTime);
    uint32 inworldTime =
        urand(sPlayerbotAIConfig.minRandomBotInWorldTime, sPlayerbotAIConfig.maxRandomBotInWorldTime);

    PlayerbotsDatabasePreparedStatement* stmt = PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_UPD_RANDOM_BOTS);
    stmt->SetData(0, randomTime);
    stmt->SetData(1, "bot_delete");
    stmt->SetData(2, bot->GetGUID().GetCounter());
    PlayerbotsDatabase.Execute(stmt);

    stmt = PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_UPD_RANDOM_BOTS);
    stmt->SetData(0, inworldTime);
    stmt->SetData(1, "logout");
    stmt->SetData(2, bot->GetGUID().GetCounter());
    PlayerbotsDatabase.Execute(stmt);

    // teleport to a random inn for bot level
    botAI->Reset(true);

    if (bot->GetGroup())
        botAI->LeaveOrDisbandGroup();

    if (pmo)
        pmo->finish();
}

void RandomPlayerbotMgr::Clear(Player* bot)
{
    PlayerbotFactory factory(bot, bot->GetLevel());
    factory.ClearEverything();
}

uint32 RandomPlayerbotMgr::GetZoneLevel(uint16 mapId, float teleX, float teleY, float /*teleZ*/)
{
    uint32 maxLevel = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);

    uint32 level = 0;
    QueryResult results = WorldDatabase.Query(
        "SELECT AVG(t.minlevel) minlevel, AVG(t.maxlevel) maxlevel FROM creature c "
        "INNER JOIN creature_template t ON c.id = t.entry WHERE map = {} AND minlevel > 1 AND ABS(position_x - {}) < "
        "{} AND ABS(position_y - {}) < {}",
        mapId, teleX, sPlayerbotAIConfig.randomBotTeleportDistance / 2, teleY,
        sPlayerbotAIConfig.randomBotTeleportDistance / 2);

    if (results)
    {
        Field* fields = results->Fetch();
        uint8 minLevel = fields[0].Get<uint8>();
        uint8 maxLevel = fields[1].Get<uint8>();
        // Creature templates whose minimum level tops their maximum pull the averages the wrong way round.
        if (minLevel > maxLevel)
            std::swap(minLevel, maxLevel);
        level = urand(minLevel, maxLevel);
        if (level > maxLevel)
            level = maxLevel;
    }
    else
    {
        level = urand(1, maxLevel);
    }

    return level;
}

void RandomPlayerbotMgr::Refresh(Player* bot)
{
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return;

    if (bot->isDead())
    {
        bot->ResurrectPlayer(1.0f);
        bot->SpawnCorpseBones();
        botAI->ResetStrategies(false);
    }

    // if (sPlayerbotAIConfig.disableRandomLevels)
    //     return;

    if (bot->InBattleground())
        return;

    LOG_DEBUG("playerbots", "Refreshing bot {} <{}>", bot->GetGUID().ToString().c_str(), bot->GetName().c_str());

    PerfMonitorOperation* pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "Refresh");

    botAI->Reset();

    bot->DurabilityRepairAll(false, 1.0f, false);
    bot->SetFullHealth();
    bot->SetPvP(sWorld->IsPvPRealm());

    // The full-bags clean-up belongs to the "maintenance" strategy, which no random bot is given: run it
    // here, with the repair. It does nothing below 90% of the bag space.
    botAI->DoSpecificAction("smart destroy item", Event(), true);

    PlayerbotFactory factory(bot, bot->GetLevel());
    factory.Refresh();

    if (bot->GetMaxPower(POWER_MANA) > 0)
        bot->SetPower(POWER_MANA, bot->GetMaxPower(POWER_MANA));

    if (bot->GetMaxPower(POWER_ENERGY) > 0)
        bot->SetPower(POWER_ENERGY, bot->GetMaxPower(POWER_ENERGY));

    uint32 money = bot->GetMoney();
    bot->SetMoney(money + 500 * sqrt(urand(1, bot->GetLevel() * 5)));

    if (bot->GetGroup())
        botAI->LeaveOrDisbandGroup();

    if (pmo)
        pmo->finish();
}

bool RandomPlayerbotMgr::IsRandomBot(Player* bot)
{
    if (bot && GET_PLAYERBOT_AI(bot))
    {
        if (IsSelfBot(bot))
            return false;
    }
    if (bot)
    {
        return IsRandomBot(bot->GetGUID().GetCounter());
    }

    return false;
}

bool RandomPlayerbotMgr::IsRandomBot(ObjectGuid::LowType bot)
{
    ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(bot);
    if (!sPlayerbotAIConfig.IsInRandomAccountList(sCharacterCache->GetCharacterAccountIdByGuid(guid)))
        return false;

    return currentBots.contains(bot);
}

bool RandomPlayerbotMgr::IsAddclassBot(Player* bot)
{
    if (bot && GET_PLAYERBOT_AI(bot))
    {
        if (IsSelfBot(bot))
            return false;
    }
    if (bot)
    {
        return IsAddclassBot(bot->GetGUID().GetCounter());
    }

    return false;
}

bool RandomPlayerbotMgr::IsAddclassBot(ObjectGuid::LowType bot)
{
    ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(bot);

    // Check the cache with faction considerations
    for (uint8 claz = CLASS_WARRIOR; claz <= CLASS_DRUID; claz++)
    {
        if (claz == 10)
            continue;

        for (uint8 isAlliance = 0; isAlliance <= 1; isAlliance++)
        {
            if (addclassCache[GetTeamClassIdx(isAlliance, claz)].find(guid) !=
                addclassCache[GetTeamClassIdx(isAlliance, claz)].end())
            {
                return true;
            }
        }
    }

    // If not in cache, check the account type
    uint32 accountId = sCharacterCache->GetCharacterAccountIdByGuid(guid);
    if (accountId && IsAccountType(accountId, 2)) // Type 2 = AddClass
    {
        return true;
    }

    return false;
}

void RandomPlayerbotMgr::GetBots()
{
    if (!currentBots.empty())
        return;

    PlayerbotsDatabasePreparedStatement* stmt =
        PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_SEL_RANDOM_BOTS_BY_OWNER_AND_EVENT);
    stmt->SetData(0, 0);
    stmt->SetData(1, "add");
    uint32 maxAllowedBotCount = GetTargetBotCount();
    if (PreparedQueryResult result = PlayerbotsDatabase.Query(stmt))
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 bot = fields[0].Get<uint32>();
            if (GetEventValue(bot, "add"))
                currentBots.insert(bot);

            if (currentBots.size() >= maxAllowedBotCount)
                break;
        } while (result->NextRow());
    }
}

std::vector<uint32> RandomPlayerbotMgr::GetBgBots(uint32 bracket)
{
    // if (!currentBgBots.empty()) return currentBgBots;

    std::vector<uint32> BgBots;

    PlayerbotsDatabasePreparedStatement* stmt =
        PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_SEL_RANDOM_BOTS_BY_EVENT_AND_VALUE);
    stmt->SetData(0, "bg");
    stmt->SetData(1, bracket);
    if (PreparedQueryResult result = PlayerbotsDatabase.Query(stmt))
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 bot = fields[0].Get<uint32>();
            BgBots.push_back(bot);
        } while (result->NextRow());
    }

    return BgBots;
}

CachedEvent* RandomPlayerbotMgr::FindEvent(uint32 bot, std::string const& event)
{
    BotEventCache& cache = eventCache[bot];

    // Load once
    if (!cache.loaded)
    {
        cache.events.clear();

        PlayerbotsDatabasePreparedStatement* stmt =
            PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_SEL_RANDOM_BOTS_BY_OWNER_AND_BOT);
        stmt->SetData(0, 0);
        stmt->SetData(1, bot);

        if (PreparedQueryResult result = PlayerbotsDatabase.Query(stmt))
        {
            do
            {
                Field* fields = result->Fetch();

                CachedEvent e;
                e.value = fields[1].Get<uint32>();
                e.lastChangeTime = fields[2].Get<uint32>();
                e.validIn = fields[3].Get<uint32>();
                e.data = fields[4].Get<std::string>();

                cache.events.emplace(fields[0].Get<std::string>(), std::move(e));
            } while (result->NextRow());
        }

        cache.loaded = true;
    }

    auto it = cache.events.find(event);
    if (it == cache.events.end())
        return nullptr;

    CachedEvent& e = it->second;

    // remove expired events
    if (e.validIn && (NowSeconds() - e.lastChangeTime) >= e.validIn &&
        event != "specNo" && event != "specLink" && event != "coa_manual_spec")
    {
        cache.events.erase(it);
        return nullptr;
    }

    return &e;
}

bool RandomPlayerbotMgr::IsSpecPvp(uint32 bot, uint8 cls)
{
    if (sPlayerbotAIConfig.coaGearByContent && GetValue(bot, "coaGearPvp"))
        return true;
    uint32 stored = GetValue(bot, "specNo");
    if (!stored)
        return false;
    uint32 specIndex = stored - 1;
    std::string const& name = sPlayerbotAIConfig.premadeSpecName[cls][specIndex];
    return !name.empty() && name.find("pvp") != std::string::npos;
}

uint32 RandomPlayerbotMgr::GetEventValue(uint32 bot, std::string const& event)
{
    std::lock_guard<std::mutex> lock(eventCacheLock);
    if (CachedEvent* e = FindEvent(bot, event))
        return e->value;

    return 0;
}

std::string RandomPlayerbotMgr::GetEventData(uint32 bot, std::string const& event)
{
    std::lock_guard<std::mutex> lock(eventCacheLock);
    if (CachedEvent* e = FindEvent(bot, event))
        return e->data;

    return "";
}

uint32 RandomPlayerbotMgr::SetEventValue(uint32 bot, std::string const& event, uint32 value, uint32 validIn,
                                         std::string const& data)
{
    PlayerbotsDatabaseTransaction trans = PlayerbotsDatabase.BeginTransaction();

    PlayerbotsDatabasePreparedStatement* stmt =
        PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_DEL_RANDOM_BOTS_BY_OWNER_AND_EVENT);
    stmt->SetData(0, 0);
    stmt->SetData(1, bot);
    stmt->SetData(2, event.c_str());
    trans->Append(stmt);

    if (value)
    {
        stmt = PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_INS_RANDOM_BOTS);
        stmt->SetData(0, 0);
        stmt->SetData(1, bot);
        stmt->SetData(2, NowSeconds());
        stmt->SetData(3, validIn);
        stmt->SetData(4, event.c_str());
        stmt->SetData(5, value);

        if (!data.empty())
            stmt->SetData(6, data.c_str());
        else
            stmt->SetData(6);  // NULL

        trans->Append(stmt);
    }

    PlayerbotsDatabase.CommitTransaction(trans);

    // Update in-memory cache
    std::lock_guard<std::mutex> lock(eventCacheLock);
    BotEventCache& cache = eventCache[bot];
    cache.loaded = true;

    if (!value)
    {
        cache.events.erase(event);
        return 0;
    }

    CachedEvent& e = cache.events[event];  // create-on-write is OK here
    e.value = value;
    e.lastChangeTime = NowSeconds();
    e.validIn = validIn;
    e.data = data;

    return value;
}

uint32 RandomPlayerbotMgr::GetValue(uint32 bot, std::string const& type) { return GetEventValue(bot, type); }

uint32 RandomPlayerbotMgr::GetValue(Player* bot, std::string const& type)
{
    return GetValue(bot->GetGUID().GetCounter(), type);
}

std::string RandomPlayerbotMgr::GetData(uint32 bot, std::string const& type) { return GetEventData(bot, type); }

void RandomPlayerbotMgr::SetValue(uint32 bot, std::string const& type, uint32 value, std::string const& data)
{
    SetEventValue(bot, type, value, sPlayerbotAIConfig.maxRandomBotInWorldTime, data);
}

void RandomPlayerbotMgr::SetValue(Player* bot, std::string const& type, uint32 value, std::string const& data)
{
    SetValue(bot->GetGUID().GetCounter(), type, value, data);
}

bool RandomPlayerbotMgr::HandlePlayerbotConsoleCommand(ChatHandler* handler, char const* args)
{
    if (!sPlayerbotAIConfig.enabled)
    {
        LOG_ERROR("playerbots", "Playerbots system is currently disabled!");
        return false;
    }

    if (!args || !*args)
    {
        LOG_ERROR("playerbots", "Usage: rndbot stats/update/reset/init/refresh/add/remove");
        return false;
    }

    std::string const cmd = args;

    if (cmd == "memory")
    {
        // Bounded, read-only sample; never evaluate calculated values or modify bot state.
        handler->PSendSysMessage("Random bots online: {}", sRandomPlayerbotMgr.playerBots.size());
        std::map<std::string, uint32> counts;
        uint32 sampled = 0;
        uint64 spells = 0;
        uint64 values = 0;
        for (auto const& [guid, bot] : sRandomPlayerbotMgr.playerBots)
        {
            PlayerbotAI* ai = GET_PLAYERBOT_AI(bot);
            if (!ai)
                continue;
            spells += bot->GetSpellMap().size();
            for (std::string const& name : ai->GetAiObjectContext()->GetValues())
            {
                ++counts[name.substr(0, name.find("::"))];
                ++values;
            }
            if (++sampled == 100)
                break;
        }
        handler->PSendSysMessage("Bot memory sample: {} bots, {} cached values, {} learned spells. "
            "Inline sizes (exclude heap allocations): Player={} AI={} Session={}",
            sampled, values, spells, sizeof(Player), sizeof(PlayerbotAI), sizeof(WorldSession));
        uint64 grids = 0;
        uint64 creatures = 0;
        uint64 gameObjects = 0;
        sMapMgr->DoForAllMaps([&](Map* map)
        {
            grids += map->GetLoadedGridsCount();
            creatures += map->GetCreatureBySpawnIdStore().size();
            gameObjects += map->GetGameObjectBySpawnIdStore().size();
        });
        handler->PSendSysMessage("Shared world: {} loaded grids, {} spawned creatures, {} spawned gameobjects",
            grids, creatures, gameObjects);
        std::vector<std::pair<std::string, uint32>> sorted(counts.begin(), counts.end());
        std::sort(sorted.begin(), sorted.end(), [](auto const& left, auto const& right)
        {
            return left.second > right.second;
        });
        for (std::size_t index = 0; index < std::min<std::size_t>(20, sorted.size()); ++index)
            handler->PSendSysMessage("Cache {}: {} instances", sorted[index].first, sorted[index].second);
        return true;
    }

    if (cmd == "reset")
    {
        PlayerbotsDatabase.Execute(PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_DEL_RANDOM_BOTS));
        {
            std::lock_guard<std::mutex> lock(sRandomPlayerbotMgr.eventCacheLock);
            sRandomPlayerbotMgr.eventCache.clear();
        }
        LOG_INFO("playerbots", "Random bots were reset for all players. Please restart the Server.");
        return true;
    }

    if (cmd == "stats")
    {
        sRandomPlayerbotMgr.PrintStats();
        // activatePrintStatsThread();
        return true;
    }

    if (cmd == "reload")
    {
        sPlayerbotAIConfig.Initialize();
        return true;
    }

    if (cmd == "update")
    {
        sRandomPlayerbotMgr.UpdateAIInternal(0);
        return true;
    }

    std::map<std::string, ConsoleCommandHandler> handlers;
    // handlers["initmin"] = &RandomPlayerbotMgr::RandomizeMin;
    handlers["init"] = &RandomPlayerbotMgr::RandomizeFirst;
    handlers["clear"] = &RandomPlayerbotMgr::Clear;
    handlers["levelup"] = handlers["level"] = &RandomPlayerbotMgr::IncreaseLevel;
    handlers["refresh"] = &RandomPlayerbotMgr::Refresh;
    handlers["teleport"] = &RandomPlayerbotMgr::RandomTeleportForLevel;
    // handlers["rpg"] = &RandomPlayerbotMgr::RandomTeleportForRpg;
    handlers["revive"] = &RandomPlayerbotMgr::Revive;
    handlers["grind"] = &RandomPlayerbotMgr::RandomTeleport;
    handlers["change_strategy"] = &RandomPlayerbotMgr::ChangeStrategy;

    for (std::map<std::string, ConsoleCommandHandler>::iterator j = handlers.begin(); j != handlers.end(); ++j)
    {
        std::string const prefix = j->first;
        if (cmd.find(prefix) != 0)
            continue;

        std::string const name = cmd.size() > prefix.size() + 1 ? cmd.substr(1 + prefix.size()) : "%";

        std::vector<uint32> botIds;
        for (std::vector<uint32>::iterator i = sPlayerbotAIConfig.randomBotAccounts.begin();
             i != sPlayerbotAIConfig.randomBotAccounts.end(); ++i)
        {
            uint32 account = *i;
            if (QueryResult results = CharacterDatabase.Query(
                    "SELECT guid FROM characters WHERE account = {} AND name like '{}'", account, name.c_str()))
            {
                do
                {
                    Field* fields = results->Fetch();

                    uint32 botId = fields[0].Get<uint32>();
                    ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(botId);
                    if (!sRandomPlayerbotMgr.IsRandomBot(guid.GetCounter()))
                    {
                        continue;
                    }
                    Player* bot = ObjectAccessor::FindPlayer(guid);
                    if (!bot)
                        continue;

                    botIds.push_back(botId);
                } while (results->NextRow());
            }
        }

        if (botIds.empty())
        {
            LOG_INFO("playerbots", "Nothing to do");
            return false;
        }

        uint32 processed = 0;
        for (std::vector<uint32>::iterator i = botIds.begin(); i != botIds.end(); ++i)
        {
            ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(*i);
            Player* bot = ObjectAccessor::FindPlayer(guid);
            if (!bot)
                continue;

            LOG_INFO("playerbots", "[{}/{}] Processing command {} for bot {}", processed++, botIds.size(), cmd.c_str(),
                     bot->GetName().c_str());

            ConsoleCommandHandler handler = j->second;
            (sRandomPlayerbotMgr.*handler)(bot);
        }

        return true;
    }

    // std::vector<std::string> messages = sRandomPlayerbotMgr.HandlePlayerbotCommand(args);
    // for (std::vector<std::string>::iterator i = messages.begin(); i != messages.end(); ++i)
    // {
    //     LOG_INFO("playerbots", "{}", i->c_str());
    // }
    return true;
}

void RandomPlayerbotMgr::HandleCommand(uint32 type, std::string const text, Player* fromPlayer, std::string channelName)
{
    // The sender needs guarding too: chatting right after a (GM/panel) teleport
    // reached LevelFor(from) with transient map/group state and crashed
    // (Andrew's repro, 2026-08-20). Symmetric to the per-bot guard below.
    if (!fromPlayer || !fromPlayer->IsInWorld() || fromPlayer->IsBeingTeleported() || !fromPlayer->FindMap())
        return;

    for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        // Bots mid-login/logout sit in this map without a usable Player or AI.
        // Chatting during a login flood walked into them: GET_PLAYERBOT_AI
        // returned null and ->HandleCommand crashed the server (SIGSEGV 0x258,
        // 2026-08-20). Skip anything not fully in the world with an attached AI.
        if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported())
            continue;

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI)
            continue;

        if (!channelName.empty())
        {
            if (ChannelMgr* cMgr = ChannelMgr::forTeam(bot->GetTeamId()))
            {
                Channel* chn = cMgr->GetChannel(channelName, bot);
                if (!chn)
                    continue;
            }
        }

        botAI->HandleCommand(type, text, fromPlayer);
    }
}

void RandomPlayerbotMgr::OnPlayerLogout(Player* player)
{
    DisablePlayerBot(player->GetGUID());

    for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (botAI && player == botAI->GetMaster())
        {
            botAI->SetMaster(nullptr);
            if (!bot->InBattleground())
            {
                botAI->ResetStrategies();
            }
        }
    }

    std::vector<Player*>::iterator i = std::find(players.begin(), players.end(), player);
    if (i != players.end())
        players.erase(i);
}

bool RandomPlayerbotMgr::CoaIsOrphanGroup(Group const* group)
{
    if (!group || group->isBGGroup() || group->isBFGroup() || group->isLFGGroup())
        return false;

    bool character = false;
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
    {
        // FindConnectedPlayer also finds a player in a battleground (another group) or on a loading screen.
        if (Player* member = ObjectAccessor::FindConnectedPlayer(slot.guid))
        {
            // A player, a selfbot, or an alt or addclass bot, which is only online with its owner.
            if (!GET_PLAYERBOT_AI(member) || !IsRandomBot(member))
                return false;
        }
        else if (!sPlayerbotAIConfig.IsInRandomAccountList(sCharacterCache->GetCharacterAccountIdByGuid(slot.guid)))
            character = true;
    }
    return character;
}

// The core keeps a player who logs out in his raid, and the bots he left there had no master and stood still
// until he came back. One look per group, never per bot, so that the whole group goes at once.
void RandomPlayerbotMgr::CoaReleaseOrphanGroups()
{
    uint32 const delay = sPlayerbotAIConfig.coaOrphanGroupReleaseDelay;
    if (!delay)
    {
        coaOrphanGroups.clear();
        return;
    }

    time_t const now = time(nullptr);
    if (now < coaOrphanCheckAt)
        return;
    coaOrphanCheckAt = now + 10;

    // Groups of bots only, groups with a player connected and groups gone since the last pass drop out of the map.
    std::unordered_set<ObjectGuid::LowType> seen;
    std::unordered_map<ObjectGuid::LowType, time_t> orphans;
    std::vector<std::pair<Group*, time_t>> due;
    for (auto const& [guid, bot] : playerBots)
    {
        Group* group = bot ? bot->GetGroup() : nullptr;
        if (!group || !seen.insert(group->GetGUID().GetCounter()).second)
            continue;

        if (!CoaIsOrphanGroup(group) || sLFGMgr->GetState(group->GetGUID()) != lfg::LFG_STATE_NONE)
            continue;

        auto const known = coaOrphanGroups.find(group->GetGUID().GetCounter());
        time_t const since = known != coaOrphanGroups.end() ? known->second : now;
        orphans.emplace(group->GetGUID().GetCounter(), since);
        if (now - since >= time_t(delay))
            due.emplace_back(group, since);
    }
    coaOrphanGroups.swap(orphans);

    uint32 released = 0;
    for (auto const& [group, since] : due)
    {
        if (released >= 1)  // one group per pass: each removal costs database queries of mod-coa-challenges
            break;

        // A member in a battleground is linked here through its original group only; a bot on its way
        // somewhere or held by a test bench goes on a later pass.
        ObjectGuid const groupGuid = group->GetGUID();
        std::vector<Player*> leaving;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (member && member->GetGroup() == group && IsRandomBot(member) && GetPlayerBot(member->GetGUID()) &&
                member->IsInWorld() && !member->IsBeingTeleported() && !RandomBotLevelMgr::IsHeld(member->GetGUID()))
                leaving.push_back(member);
        }
        if (leaving.empty())
            continue;
        ++released;

        // A leave as on /leave; the last ones disband the group, so it is never touched after the first one.
        for (Player* member : leaving)
            if (Group* current = member->GetGroup(); current && current->GetGUID() == groupGuid)
                member->RemoveFromGroup(GROUP_REMOVEMETHOD_LEAVE);

        uint32 left = 0;
        for (Player* member : leaving)
        {
            if (member->GetGroup())
                continue;

            ++left;
            // Back to the life of a random bot, as UpdateAIGroupMaster does for a bot out of its group.
            if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(member))
            {
                botAI->SetMaster(nullptr);
                botAI->Reset(true);
                botAI->ResetStrategies();
            }
        }
        LOG_INFO("playerbots", "coa: released {} bots of a group whose players are offline for {}s", left,
                 uint32(now - since));
    }
}

// Spells and auras of the CoA rulesets (core: src/server/coa/AscensionRulesets.cpp, ApplyRuleset).
namespace
{
constexpr uint32 COA_RULESET_HIGH_RISK = 1004019;
constexpr uint32 COA_RULESET_WAR_MODE = 1004119;
constexpr uint32 COA_RULESET_PVE = 9931032;
constexpr uint32 COA_RULESET_MERCENARY = 9930874;
}

// The core puts every character without a ruleset in PvE at login, bots included. With
// AiPlayerbot.CoaRulesetForBots, a share of the random bots is put in High Risk or War Mode instead: the bucket
// comes from the guid, so a bot keeps its ruleset from one login to the next, and changing the shares moves
// only the bots whose bucket changed side. Not in a battleground, in combat or dead.
void RandomPlayerbotMgr::CoaApplyRuleset(Player* bot)
{
    if (!sPlayerbotAIConfig.coaRulesetForBots || !bot || !bot->IsInWorld() || !IsRandomBot(bot))
        return;
    if (bot->InBattleground() || bot->InArena() || bot->IsInCombat() || !bot->IsAlive())
        return;

    uint32 const highRisk = std::min<uint32>(sPlayerbotAIConfig.coaRulesetHighRiskPct, 100);
    uint32 const warMode = std::min<uint32>(sPlayerbotAIConfig.coaRulesetWarModePct, 100 - highRisk);
    uint32 const bucket = (bot->GetGUID().GetCounter() * 2654435761u) % 100;
    uint32 const wanted = bucket < highRisk ? COA_RULESET_HIGH_RISK
                        : bucket < highRisk + warMode ? COA_RULESET_WAR_MODE
                        : COA_RULESET_PVE;

    bool const hasHighRisk = bot->HasAura(COA_RULESET_HIGH_RISK);
    bool const hasPve = bot->HasAura(COA_RULESET_PVE);
    bool const hasWarMode = bot->HasAura(COA_RULESET_WAR_MODE);
    bool const ok = wanted == COA_RULESET_HIGH_RISK ? hasHighRisk && !hasPve && !hasWarMode
                  : wanted == COA_RULESET_WAR_MODE ? hasWarMode && !hasPve && !hasHighRisk
                  : hasPve && !hasHighRisk;
    if (!ok)
    {
        for (uint32 spell : {COA_RULESET_HIGH_RISK, COA_RULESET_WAR_MODE, COA_RULESET_PVE, COA_RULESET_MERCENARY})
            bot->RemoveAurasDueToSpell(spell);
        bot->CastSpell(bot, wanted, true);
        LOG_DEBUG("playerbots", "coa: {} ruleset {}", bot->GetName(),
                  wanted == COA_RULESET_HIGH_RISK ? "High Risk" : wanted == COA_RULESET_WAR_MODE ? "War Mode" : "PvE");
    }

    // The core only puts the aura: on a PvE realm a High Risk or War Mode character was never flagged for PvP.
    // These bots keep the PvP flag on, as the rulesets mean on Ascension; PvE bots let it run out.
    bool const pvp = wanted != COA_RULESET_PVE;
    if (pvp && !bot->HasPlayerFlag(PLAYER_FLAGS_IN_PVP))
        bot->SetPlayerFlag(PLAYER_FLAGS_IN_PVP);
    else if (!pvp && bot->HasPlayerFlag(PLAYER_FLAGS_IN_PVP))
        bot->RemovePlayerFlag(PLAYER_FLAGS_IN_PVP);
    if (pvp && !bot->IsPvP())
        bot->UpdatePvP(true, true);
}

void RandomPlayerbotMgr::CoaUpdateGear(Player* bot)
{
    // Any bot of a random bot account, those the LFG recruits out of the active list included; never a player's own
    // characters.
    if (!bot || !bot->IsInWorld() || !bot->IsAlive() || bot->IsInCombat() || bot->IsInFlight() || bot->GetLevel() < 10 ||
        !sPlayerbotAIConfig.IsInRandomAccountList(bot->GetSession()->GetAccountId()))
        return;
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return;

    // The player who groups the bot sets its item level; a High Risk bot stays in PvP gear with them.
    // AiPlayerbot.CoaGearMatchGroup = 0: a grouped bot keeps the gear it has, as on its own.
    Player* master = botAI->GetMaster();
    if (!sPlayerbotAIConfig.coaGearMatchGroup || !master || master == bot || GET_PLAYERBOT_AI(master) ||
        !bot->GetGroup() || master->GetGroup() != bot->GetGroup())
        master = nullptr;

    // AiPlayerbot.CoaGearHighRiskPvp = 0: a High Risk bot wears PvP gear only in a battleground or an arena, as the
    // others (the test servers' boss bench measures every bot in PvE gear).
    bool const pvp = (sPlayerbotAIConfig.coaGearHighRiskPvp && bot->HasAura(COA_RULESET_HIGH_RISK)) ||
                     bot->InBattleground() || bot->InArena() || bot->InBattlegroundQueue();

    // The level 60 honor sets: Knight-Lieutenant's (rare 66), Knight-Captain's (rare 68), Lieutenant Commander's
    // (rare 71), Marshal's (epic 71), Field Marshal's (epic 74) and their Horde counterparts.
    static std::array<std::pair<uint32, uint32>, 5> const tiers = {{
        { ITEM_QUALITY_RARE, 66 }, { ITEM_QUALITY_RARE, 68 }, { ITEM_QUALITY_RARE, 71 },
        { ITEM_QUALITY_EPIC, 71 }, { ITEM_QUALITY_EPIC, 74 } }};
    uint32 quality = 0;
    uint32 itemLevel = 0;
    if (master)
    {
        // The player's average item level, compared item by item: a gear score weighed by quality let green
        // items far above it in (a bot at 88 for a player at 72). In PvP, never under the first honor tier.
        uint32 sum = 0;
        uint32 count = 0;
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
            if (slot != EQUIPMENT_SLOT_BODY && slot != EQUIPMENT_SLOT_TABARD)
                if (Item* item = master->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                {
                    // A piece that scales with its wearer (heirlooms, CoA leveling legendaries) has a tiny base item
                    // level: it counts as a piece of the player's level, or the bots of a player in them stood naked.
                    ItemTemplate const* proto = item->GetTemplate();
                    // Test items far above any level (ilvl 435, 65535) are held to twice the player's level.
                    bool const scales = proto->ScalingStatDistribution || proto->Quality == ITEM_QUALITY_HEIRLOOM ||
                                        (proto->Quality >= ITEM_QUALITY_LEGENDARY && proto->ItemLevel < master->GetLevel());
                    uint32 const level = scales ? std::max<uint32>(proto->ItemLevel, master->GetLevel() + 5) : proto->ItemLevel;
                    sum += std::min<uint32>(level, master->GetLevel() * 2 + 20);
                    ++count;
                }
        if (!count)
            return;
        quality = ITEM_QUALITY_EPIC;
        // Never under the item level of the bot's own level: items carry about five more than their required level,
        // and a player at item level 22 left level 22 bots with nothing to wear (PTR, 07/10).
        itemLevel = std::max<uint32>(sum / count, bot->GetLevel() + 5);
        if (pvp && bot->GetLevel() >= 60)
            itemLevel = std::max(itemLevel, tiers[0].second);
    }
    else if (pvp && bot->GetLevel() >= 60)
    {
        // The tier follows the bot's best arena rating; without a team it is drawn once from the guid.
        uint32 rating = 0;
        for (uint8 slot = 0; slot < MAX_ARENA_SLOT; ++slot)
            if (ArenaTeam* team = sArenaTeamMgr->GetArenaTeamById(bot->GetArenaTeamId(slot)))
                rating = std::max<uint32>(rating, team->GetRating());
        uint32 const tier = rating ? (rating < 1300 ? 0 : rating < 1500 ? 1 : rating < 1700 ? 2 : rating < 1900 ? 3 : 4)
                                   : (bot->GetGUID().GetCounter() * 2654435761u) % 4;
        quality = tiers[tier].first;
        itemLevel = tiers[tier].second;
    }

    uint32 const botId = bot->GetGUID().GetCounter();
    if (GetValue(botId, "coaGearPvp") == uint32(pvp) && GetValue(botId, "coaGearScore") == itemLevel)
        return;

    // A bot on its own keeps its PvP gear for 10 minutes out of a battleground and its queue: most queue again at
    // once, and each battleground cost two changes of some seventeen items (18,000 changes a night on the NUC).
    if (pvp)
        botAI->coaGearPvpSeenAt = getMSTime();
    else if (!master && botAI->coaGearPvpSeenAt && GetMSTimeDiffToNow(botAI->coaGearPvpSeenAt) < 10 * MINUTE * IN_MILLISECONDS)
        return;

    // Each change destroys and creates some seventeen items: one bot at a time on the whole server.
    static std::atomic<uint32> lastRegear{0};
    uint32 last = lastRegear.load();
    // A bot grouped with a player goes first: it waited behind the High Risk bots regeared after a restart.
    if (!master && (getMSTimeDiff(last, getMSTime()) < 1000 || !lastRegear.compare_exchange_strong(last, getMSTime())))
        return;

    uint32 const startedAt = getMSTime();
    SetEventValue(botId, "coaGearPvp", pvp, 30 * DAY);
    SetEventValue(botId, "coaGearScore", itemLevel, 30 * DAY);
    PlayerbotFactory::DestroyEquippedGear(bot);
    PlayerbotFactory factory(bot, bot->GetLevel(), quality, 0);
    factory.maxItemLevel = itemLevel;
    factory.preferResilience = pvp;
    factory.InitEquipment(false, false);
    // A slot nothing filled under the limit is filled without it: a bot is never left without gear.
    uint32 empty = 0;
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        if (slot != EQUIPMENT_SLOT_BODY && slot != EQUIPMENT_SLOT_TABARD && !bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            ++empty;
    if (empty > 3)
    {
        factory.maxItemLevel = 0;
        factory.InitEquipment(true, false);
    }
    factory.InitAmmo();
    if (bot->GetLevel() >= sPlayerbotAIConfig.minEnchantingBotLevel)
        factory.ApplyEnchantAndGemsNew();
    bot->DurabilityRepairAll(false, 1.0f, false);
    LOG_INFO("playerbots", "coa gear: {} {} gear, item level limit {}{}, {} ms", bot->GetName(), pvp ? "PvP" : "PvE",
             itemLevel, master ? " (grouped)" : "", GetMSTimeDiffToNow(startedAt));
}

void RandomPlayerbotMgr::OnBotLoginInternal(Player* const bot)
{
    if (_isBotLogging)
    {
        LOG_INFO("playerbots", "{}/{} Bot {} logged in", playerBots.size(),
                 sRandomPlayerbotMgr.GetMaxAllowedBotCount(), bot->GetName().c_str());

        if (playerBots.size() == sRandomPlayerbotMgr.GetMaxAllowedBotCount())
        {
            _isBotLogging = false;
        }
    }

    // Run guild recovery/assignment at login to handle empty guild tables after restart.
    if (sPlayerbotAIConfig.randomBotGuildCount > 0)
    {
        PlayerbotFactory factory(bot, bot->GetLevel());
        factory.InitGuild();
    }

    RandomPlayerbotFactory::AssignBotToArenaTeam(bot);
    CoaApplyRuleset(bot);

    // CoA starts Blood Elves in Deathknell and Draenei in Shadowglen (jealous-sound/azerothcore-wotlk-coa#6397). Bots
    // made before stayed at Sunstrider Isle or Ammen Vale, where no CoA quest awaits them, and stood there (Discord
    // 07/10): a low level random bot in Outland's map goes to its race's start.
    if (bot->GetMapId() == 530 && bot->GetLevel() <= 20 && IsRandomBot(bot) && !bot->IsInCombat() && !bot->GetGroup())
        if (PlayerInfo const* info = sObjectMgr->GetPlayerInfo(bot->getRace(), bot->getClass()))
            if (info->mapId != 530)
            {
                LOG_INFO("playerbots", "coa: {} (level {}) leaves map 530 for its race's start", bot->GetName(),
                         bot->GetLevel());
                bot->TeleportTo(info->mapId, info->positionX, info->positionY, info->positionZ, info->orientation);
            }

    // A bot made before MaxPlayerLevel or RandomBotMaxLevel was lowered kept its level, since Randomize only
    // rebuilds a bot at the level it has: bring it down to the cap, gear, talents and spells redone.
    if (IsRandomBot(bot) && !bot->InBattleground() && !bot->InArena() && GET_PLAYERBOT_AI(bot))
    {
        uint32 const levelCap = std::min<uint32>(sPlayerbotAIConfig.randomBotMaxLevel,
                                                 sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL));
        if (levelCap && bot->GetLevel() > levelCap)
        {
            uint32 const oldLevel = bot->GetLevel();
            if (bot->IsMounted())
                bot->Dismount();
            SetValue(bot, "level", levelCap);
            PlayerbotFactory factory(bot, levelCap);
            factory.Randomize(false);
            RandomTeleportForLevel(bot);
            LOG_INFO("playerbots", "coa: {} was level {}, above the cap {}: rebuilt at level {}", bot->GetName(),
                     oldLevel, levelCap, levelCap);
        }
    }

    // Once per bot for 2.0, a repair in place of a purge, which would lose the arena teams, ratings, PvP statistics
    // and achievements: a bot made by older versions loses the mounts and companions it piled up (1,224 on
    // average before jealous-sound#5107, each reloaded at every login), and one wearing gear above its level is
    // geared again.
    if (IsRandomBot(bot) && GetValue(bot, "coaRepaired") < 20)
    {
        SetEventValue(bot->GetGUID().GetCounter(), "coaRepaired", 20, 365 * DAY);
        std::vector<uint32> companions;
        std::vector<uint32> mounts;
        for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
            if (playerSpell->State != PLAYERSPELL_REMOVED)
            {
                SkillLineAbilityMapBounds const bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
                for (auto itr = bounds.first; itr != bounds.second; ++itr)
                {
                    if (itr->second->SkillLine == SKILL_COMPANIONS)
                        companions.push_back(spellId);
                    else if (itr->second->SkillLine == SKILL_MOUNTS)
                        mounts.push_back(spellId);
                }
            }
        // A bot rides one mount: it keeps a few, the companions all go.
        size_t const keptMounts = 4;
        if (companions.size() + mounts.size() > 20)
        {
            for (uint32 const spellId : companions)
                bot->removeSpell(spellId, SPEC_MASK_ALL, false);
            for (size_t i = keptMounts; i < mounts.size(); ++i)
                bot->removeSpell(mounts[i], SPEC_MASK_ALL, false);
            LOG_INFO("playerbots", "coa repair: {} drops {} companions and {} mounts", bot->GetName(), companions.size(),
                     mounts.size() > keptMounts ? mounts.size() - keptMounts : 0);
        }
        bool overLevel = false;
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END && !overLevel; ++slot)
            if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                overLevel = item->GetTemplate()->RequiredLevel > bot->GetLevel();
        if (overLevel)
        {
            PlayerbotFactory::DestroyEquippedGear(bot);
            PlayerbotFactory factory(bot, bot->GetLevel());
            factory.InitEquipment(false, false);
            factory.InitAmmo();
            LOG_INFO("playerbots", "coa repair: {} wore gear above level {}: geared again", bot->GetName(), bot->GetLevel());
        }
    }

    // The core resets at login the talents of a character that spent more than its level allows (bots made by older
    // versions): the bot then stood without talents until its next randomize, up to 14 days later. It picks them now.
    // A CoA class has no talent tree but Character Advancement entries, set from our level builds: they were set only
    // on a level up or a change of specialization, so a bot made by an older version kept entries above its level or
    // missing ones. ApplyCoaTalents brings them to the build of its level, down and up.
    if (IsAscensionCustomClassId(bot->getClass()))
    {
        if (uint32 const changed = ApplyCoaTalents(bot))
            LOG_INFO("playerbots", "coa repair: {} (level {}) had {} Character Advancement entries off its build",
                     bot->GetName(), bot->GetLevel(), changed);
    }
    else if (IsRandomBot(bot) && bot->GetFreeTalentPoints() >= 5)
    {
        uint32 const freePoints = bot->GetFreeTalentPoints();
        PlayerbotFactory factory(bot, bot->GetLevel());
        factory.InitTalentsTree(true);
        LOG_INFO("playerbots", "coa repair: {} had {} free talent points: talents picked again", bot->GetName(), freePoints);
    }

    // Bots rebuilt under level 5 before 1.5 lost their CoA starter kit and still stand naked.
    PlayerbotFactory::DressNakedCoaStarter(bot);
    // And those brought down in level before 1.5.1 still carry the spell ranks of their old level.
    if (uint32 const dropped = PlayerbotFactory::DropCoaRanksAbove(bot, bot->GetLevel()))
        LOG_INFO("playerbots", "{} (level {}): {} spell ranks above its level removed", bot->GetName(),
                 uint32(bot->GetLevel()), dropped);

    if (sPlayerbotAIConfig.randomBotFixedLevel)
    {
        bot->SetPlayerFlag(PLAYER_FLAGS_NO_XP_GAIN);
    }
    else
    {
        bot->RemovePlayerFlag(PLAYER_FLAGS_NO_XP_GAIN);
    }
}

void RandomPlayerbotMgr::OnPlayerLogin(Player* player)
{
    uint32 botsNearby = 0;

    for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        if (player == bot /* || GET_PLAYERBOT_AI(player)*/)  // TEST
            continue;

        Cell playerCell(player->GetPositionX(), player->GetPositionY());
        Cell botCell(bot->GetPositionX(), bot->GetPositionY());

        // if (playerCell == botCell)
        // botsNearby++;

        Group* group = bot->GetGroup();
        if (!group)
            continue;

        for (GroupReference* gref = group->GetFirstMember(); gref; gref = gref->next())
        {
            Player* member = gref->GetSource();
            PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
            if (botAI && member == player && (!botAI->GetMaster() || GET_PLAYERBOT_AI(botAI->GetMaster())))
            {
                if (!bot->InBattleground())
                {
                    botAI->SetMaster(player);
                    botAI->ResetStrategies();
                    botAI->TellMasterGreeting(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                        "hello", "Hello", {}));
                }

                break;
            }
        }
    }

    if (botsNearby > 100 && false)
    {
        WorldPosition botPos(player);

        // botPos.GetReachableRandomPointOnGround(player, sPlayerbotAIConfig.reactDistance * 2, true);

        // player->TeleportTo(botPos);
        // player->Relocate(botPos.coord_x, botPos.coord_y, botPos.coord_z, botPos.orientation);

        if (!player->GetFactionTemplateEntry())
        {
            botPos.GetReachableRandomPointOnGround(player, sPlayerbotAIConfig.reactDistance * 2, true);
        }
        else
        {
            std::vector<TravelDestination*> dests = TravelMgr::instance().getRpgTravelDestinations(player, true, true, 200000.0f);

            // No destination at all: drawing one from the empty list read out of it.
            if (dests.empty())
                botPos.GetReachableRandomPointOnGround(player, sPlayerbotAIConfig.reactDistance * 2, true);
            else
            do
            {
                RpgTravelDestination* dest = (RpgTravelDestination*)dests[urand(0, dests.size() - 1)];
                CreatureTemplate const* cInfo = dest->GetCreatureTemplate();
                if (!cInfo)
                    continue;

                FactionTemplateEntry const* factionEntry = sFactionTemplateStore.LookupEntry(cInfo->faction);
                ReputationRank reaction = Unit::GetFactionReactionTo(player->GetFactionTemplateEntry(), factionEntry);

                if (reaction > REP_NEUTRAL && dest->nearestPoint(&botPos)->GetMapId() == player->GetMapId())
                {
                    botPos = *dest->nearestPoint(&botPos);
                    break;
                }
            } while (true);
        }

        player->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TELEPORTED | AURA_INTERRUPT_FLAG_CHANGE_MAP);
        player->TeleportTo(botPos);

        // player->Relocate(botPos.getX(), botPos.getY(), botPos.getZ(), botPos.getO());
    }

    if (IsRandomBot(player))
    {
        // ObjectGuid::LowType guid = player->GetGUID().GetCounter(); //not used, conditional could be rewritten for
        // simplicity. line marked for removal.
        player->SetPvP(sWorld->IsPvPRealm());
    }
    else
    {
        players.push_back(player);
        LOG_DEBUG("playerbots", "Including non-random bot player {} into random bot update", player->GetName().c_str());
    }
}

void RandomPlayerbotMgr::OnPlayerLoginError(uint32 bot)
{
    SetEventValue(bot, "add", 0, 0);
    currentBots.erase(bot);
}

Player* RandomPlayerbotMgr::GetRandomPlayer()
{
    if (players.empty())
        return nullptr;

    uint32 index = urand(0, players.size() - 1);
    return players[index];
}

void RandomPlayerbotMgr::PrintStats()
{
    printStatsTimer = time(nullptr);
    LOG_INFO("playerbots", "Random Bots Stats: {} online", playerBots.size());

    std::map<uint8, uint32> alliance, horde;
    for (uint32 i = 0; i < 10; ++i)
    {
        alliance[i] = 0;
        horde[i] = 0;
    }

    std::map<uint8, uint32> perRace;
    std::map<uint8, uint32> perClass;

    std::map<uint8, uint32> lvlPerRace;
    std::map<uint8, uint32> lvlPerClass;
    for (uint8 race = RACE_HUMAN; race < sRaceMgr->GetMaxRaces(); ++race)
    {
        perRace[race] = 0;
        lvlPerRace[race] = 0;
    }

    for (uint8 cls = CLASS_WARRIOR; cls < MAX_CLASSES; ++cls)
    {
        perClass[cls] = 0;
        lvlPerClass[cls] = 0;
    }

    uint32 dps = 0;
    uint32 heal = 0;
    uint32 tank = 0;
    uint32 active = 0;
/*    uint32 update = 0;
    uint32 randomize = 0;
    uint32 teleport = 0;
    uint32 changeStrategy = 0;*/
    uint32 dead = 0;
    uint32 combat = 0;
    // uint32 revive = 0; //not used, line marked for removal.
    uint32 inFlight = 0;
    uint32 moving = 0;
    uint32 mounted = 0;
    uint32 inBg = 0;
    uint32 rest = 0;
    uint32 engine_noncombat = 0;
    uint32 engine_combat = 0;
    uint32 engine_dead = 0;
    std::unordered_map<NewRpgStatus, int> rpgStatusCount;
    uint32 botsInCity = 0;
    // static NewRpgStatistic rpgStasticTotal;
    std::unordered_map<uint32, int> zoneCount;
    uint8 maxBotLevel = 0;
    for (PlayerBotMap::iterator i = playerBots.begin(); i != playerBots.end(); ++i)
    {
        Player* bot = i->second;
        if (IsAlliance(bot->getRace()))
            ++alliance[bot->GetLevel()];
        else
            ++horde[bot->GetLevel()];
        maxBotLevel = std::max(maxBotLevel, bot->GetLevel());

        ++perRace[bot->getRace()];
        ++perClass[bot->getClass()];

        lvlPerClass[bot->getClass()] += bot->GetLevel();
        lvlPerRace[bot->getRace()] += bot->GetLevel();

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI)
        {
            LOG_ERROR("playerbots", "Player/Bot {} is registered in sRandomPlayerbotMgr playerBots and has no bot AI!", bot->GetName().c_str());
            continue;
        }

        if (botAI->IsActivityAllowedCached())
            ++active;
        /* TODO: Review statistics on rpg merge
        if (botAI->GetAiObjectContext()->GetValue<bool>("random bot update")->Get())
            ++update;

        uint32 botId = bot->GetGUID().GetCounter();
        if (!GetEventValue(botId, "randomize"))
            ++randomize;

        if (!GetEventValue(botId, "teleport"))
            ++teleport;

        if (!GetEventValue(botId, "change_strategy"))
            ++changeStrategy;
        */
        if (bot->isDead())
        {
            ++dead;
            // if (!GetEventValue(botId, "dead"))
            //++revive;
        }
        if (bot->IsInCombat())
            ++combat;

        if (bot->isMoving())
            ++moving;

        if (bot->IsInFlight())
            ++inFlight;

        if (bot->IsMounted())
            ++mounted;

        if (bot->InBattleground() || bot->InArena())
            ++inBg;

        if (bot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING))
            ++rest;

        if (botAI->GetState() == BOT_STATE_NON_COMBAT)
            ++engine_noncombat;

        else if (botAI->GetState() == BOT_STATE_COMBAT)
            ++engine_combat;

        else
            ++engine_dead;

        if (botAI->IsHeal(bot, false))
            ++heal;

        else if (botAI->IsTank(bot, false))
            ++tank;

        else
            ++dps;

        zoneCount[bot->GetZoneId()]++;

        if (sPlayerbotAIConfig.enableNewRpgStrategy)
        {
            rpgStatusCount[botAI->rpgInfo.GetStatus()]++;
            if (botAI->rpgInfo.cityStayMs)
                ++botsInCity;
            rpgStasticTotal += botAI->rpgStatistic;
            botAI->rpgStatistic = NewRpgStatistic();
        }
    }

    LOG_INFO("playerbots", "Bots level:");
    // uint32 maxLevel = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
    uint32_t currentAlliance = 0, currentHorde = 0;
    uint32_t step = std::max(1, static_cast<int>((maxBotLevel + 4) / 8));
    uint32_t from = 1;

    for (uint8 i = 1; i <= maxBotLevel; ++i)
    {
        currentAlliance += alliance[i];
        currentHorde += horde[i];

        if (((i + 1) % step == 0) || i == maxBotLevel)
        {
            if (currentAlliance || currentHorde)
                LOG_INFO("playerbots", "    {}..{}: {} alliance, {} horde", from, i, currentAlliance, currentHorde);
            currentAlliance = 0;
            currentHorde = 0;
            from = i + 1;
        }
    }

    LOG_INFO("playerbots", "Bots race:");
    for (uint8 race = RACE_HUMAN; race < sRaceMgr->GetMaxRaces(); ++race)
    {
        if (perRace[race])
        {
            uint32 lvl = lvlPerRace[race] * 10 / perRace[race];
            float flvl = lvl / 10.0f;
            LOG_INFO("playerbots", "    {}: {}, avg lvl: {}", ChatHelper::FormatRace(race).c_str(), perRace[race],
                     flvl);
        }
    }

    LOG_INFO("playerbots", "Bots class:");
    for (uint8 cls = CLASS_WARRIOR; cls < MAX_CLASSES; ++cls)
    {
        if (perClass[cls])
        {
            uint32 lvl = lvlPerClass[cls] * 10 / perClass[cls];
            float flvl = lvl / 10.0f;
            LOG_INFO("playerbots", "    {}: {}, avg lvl: {}", ChatHelper::FormatClass(cls).c_str(), perClass[cls],
                     flvl);
        }
    }

    LOG_INFO("playerbots", "Bots role:");
    LOG_INFO("playerbots", "    tank: {}, heal: {}, dps: {}", tank, heal, dps);

    LOG_INFO("playerbots", "Bots status:");
    LOG_INFO("playerbots", "    Active: {}", active);
    LOG_INFO("playerbots", "    Moving: {}", moving);

    // LOG_INFO("playerbots", "Bots to:");
    // LOG_INFO("playerbots", "    update: {}", update);
    // LOG_INFO("playerbots", "    randomize: {}", randomize);
    // LOG_INFO("playerbots", "    teleport: {}", teleport);
    // LOG_INFO("playerbots", "    change_strategy: {}", changeStrategy);
    // LOG_INFO("playerbots", "    revive: {}", revive);

    LOG_INFO("playerbots", "    In flight: {}", inFlight);
    LOG_INFO("playerbots", "    On mount: {}", mounted);
    LOG_INFO("playerbots", "    In combat: {}", combat);
    LOG_INFO("playerbots", "    In BG: {}", inBg);
    LOG_INFO("playerbots", "    In Rest: {}", rest);
    LOG_INFO("playerbots", "    Dead: {}", dead);

    if (sPlayerbotAIConfig.enableNewRpgStrategy)
    {
        LOG_INFO("playerbots", "Bots rpg status:");
        LOG_INFO("playerbots",
                 "    Idle: {}, Rest: {}, GoGrind: {}, GoCamp: {}, MoveRandom: {}, MoveNpc: {}, DoQuest: {}, "
                 "TravelFlight: {}, OutdoorPvP: {}, GoCity: {}, InCity: {}",
                 rpgStatusCount[RPG_IDLE], rpgStatusCount[RPG_REST], rpgStatusCount[RPG_GO_GRIND],
                 rpgStatusCount[RPG_GO_CAMP], rpgStatusCount[RPG_WANDER_RANDOM], rpgStatusCount[RPG_WANDER_NPC],
                 rpgStatusCount[RPG_DO_QUEST], rpgStatusCount[RPG_TRAVEL_FLIGHT], rpgStatusCount[RPG_OUTDOOR_PVP],
                 rpgStatusCount[RPG_GO_CITY], botsInCity);

        LOG_INFO("playerbots", "Bots total quests:");
        LOG_INFO("playerbots", "    Accepted: {}, Rewarded: {}, Dropped: {}", rpgStasticTotal.questAccepted,
                 rpgStasticTotal.questRewarded, rpgStasticTotal.questDropped);
    }

    LOG_INFO("playerbots", "Bots engine:", dead);
    LOG_INFO("playerbots", "    Non-combat: {}, Combat: {}, Dead: {}", engine_noncombat, engine_combat, engine_dead);
}

double RandomPlayerbotMgr::GetBuyMultiplier(Player* bot)
{
    uint32 id = bot->GetGUID().GetCounter();
    uint32 value = GetEventValue(id, "buymultiplier");
    if (!value)
    {
        value = urand(50, 120);
        uint32 validIn = urand(sPlayerbotAIConfig.minRandomBotsPriceChangeInterval,
                               sPlayerbotAIConfig.maxRandomBotsPriceChangeInterval);
        SetEventValue(id, "buymultiplier", value, validIn);
    }

    return (double)value / 100.0;
}

double RandomPlayerbotMgr::GetSellMultiplier(Player* bot)
{
    uint32 id = bot->GetGUID().GetCounter();
    uint32 value = GetEventValue(id, "sellmultiplier");
    if (!value)
    {
        value = urand(80, 250);
        uint32 validIn = urand(sPlayerbotAIConfig.minRandomBotsPriceChangeInterval,
                               sPlayerbotAIConfig.maxRandomBotsPriceChangeInterval);
        SetEventValue(id, "sellmultiplier", value, validIn);
    }

    return (double)value / 100.0;
}

void RandomPlayerbotMgr::AddTradeDiscount(Player* bot, Player* master, int32 value)
{
    if (!master)
        return;

    uint32 discount = GetTradeDiscount(bot, master);
    int32 result = (int32)discount + value;
    discount = (result < 0 ? 0 : result);

    SetTradeDiscount(bot, master, discount);
}

void RandomPlayerbotMgr::SetTradeDiscount(Player* bot, Player* master, uint32 value)
{
    if (!master)
        return;

    uint32 botId = bot->GetGUID().GetCounter();
    uint32 masterId = master->GetGUID().GetCounter();

    std::ostringstream name;
    name << "trade_discount_" << masterId;
    SetEventValue(botId, name.str(), value, sPlayerbotAIConfig.maxRandomBotInWorldTime);
}

uint32 RandomPlayerbotMgr::GetTradeDiscount(Player* bot, Player* master)
{
    if (!master)
        return 0;

    uint32 botId = bot->GetGUID().GetCounter();
    uint32 masterId = master->GetGUID().GetCounter();

    std::ostringstream name;
    name << "trade_discount_" << masterId;
    return GetEventValue(botId, name.str());
}

std::string const RandomPlayerbotMgr::HandleRemoteCommand(std::string const request)
{
    std::string::const_iterator pos = std::find(request.begin(), request.end(), ',');
    if (pos == request.end())
    {
        std::ostringstream out;
        out << "invalid request: " << request;
        return out.str();
    }

    std::string const command = std::string(request.begin(), pos);
    ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(atoi(std::string(pos + 1, request.end()).c_str()));
    Player* bot = GetPlayerBot(guid);
    if (!bot)
        return "invalid guid";

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return "invalid guid";

    return botAI->HandleRemoteCommand(command);
}

void RandomPlayerbotMgr::ChangeStrategy(Player* player)
{
    uint32 bot = player->GetGUID().GetCounter();

    if (frand(0.f, 100.f) > sPlayerbotAIConfig.randomBotRpgChance)
    {
        LOG_INFO("playerbots", "Bot #{} <{}>: sent to grind spot", bot, player->GetName().c_str());
        ScheduleTeleport(bot, 30);
    }
    else
    {
        LOG_INFO("playerbots", "Changing strategy for bot #{} <{}> to RPG", bot, player->GetName().c_str());
        LOG_INFO("playerbots", "Bot #{} <{}>: sent to inn", bot, player->GetName().c_str());
        RandomTeleportForLevel(player);
        SetEventValue(bot, "teleport", 1, sPlayerbotAIConfig.maxRandomBotInWorldTime);
    }

    ScheduleChangeStrategy(bot);
}

void RandomPlayerbotMgr::ChangeStrategyOnce(Player* player)
{
    uint32 bot = player->GetGUID().GetCounter();

    if (frand(0.f, 100.f) > sPlayerbotAIConfig.randomBotRpgChance)  // select grind / pvp
    {
        LOG_INFO("playerbots", "Bot #{} <{}>: sent to grind spot", bot, player->GetName().c_str());
        RandomTeleportForLevel(player);
        Refresh(player);
    }
    else
    {
        LOG_INFO("playerbots", "Bot #{} <{}>: sent to inn", bot, player->GetName().c_str());
        RandomTeleportForLevel(player);
    }
}

void RandomPlayerbotMgr::RandomTeleportForRpg(Player* bot)
{
    uint32 race = bot->getRace();
    uint32 level = bot->GetLevel();
    LOG_DEBUG("playerbots", "Random teleporting bot {} for RPG ({} locations available)", bot->GetName().c_str(),
              rpgLocsCacheLevel[race].size());
    RandomTeleport(bot, rpgLocsCacheLevel[race][level], true);
}

void RandomPlayerbotMgr::Remove(Player* bot)
{
    ObjectGuid owner = bot->GetGUID();

    PlayerbotsDatabasePreparedStatement* stmt =
        PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_DEL_RANDOM_BOTS_BY_OWNER);
    stmt->SetData(0, 0);
    stmt->SetData(1, owner.GetCounter());
    PlayerbotsDatabase.Execute(stmt);

    uint32 botId = owner.GetCounter();
    {
        std::lock_guard<std::mutex> lock(eventCacheLock);
        eventCache.erase(botId);
    }

    LogoutPlayerBot(owner);
}

CreatureData const* RandomPlayerbotMgr::GetCreatureDataByEntry(uint32 entry)
{
    if (entry != 0)
    {
        for (auto const& itr : sObjectMgr->GetAllCreatureData())
            if (itr.second.id == entry)
                return &itr.second;
    }

    return nullptr;
}

ObjectGuid RandomPlayerbotMgr::GetBattleMasterGUID(Player* bot, BattlegroundTypeId bgTypeId)
{
    ObjectGuid battleMasterGUID = ObjectGuid::Empty;

    TeamId team = bot->GetTeamId();
    std::vector<uint32> Bms;

    for (auto i = std::begin(BattleMastersCache[team][bgTypeId]); i != std::end(BattleMastersCache[team][bgTypeId]);
         ++i)
    {
        Bms.insert(Bms.end(), *i);
    }

    for (auto i = std::begin(BattleMastersCache[TEAM_NEUTRAL][bgTypeId]);
         i != std::end(BattleMastersCache[TEAM_NEUTRAL][bgTypeId]); ++i)
    {
        Bms.insert(Bms.end(), *i);
    }

    if (Bms.empty())
        return battleMasterGUID;

    float dist1 = FLT_MAX;

    for (auto i = begin(Bms); i != end(Bms); ++i)
    {
        CreatureData const* data = nullptr;
        auto spawnIt = BattleMasterSpawnIds.find(*i);
        if (spawnIt == BattleMasterSpawnIds.end())
            data = sRandomPlayerbotMgr.GetCreatureDataByEntry(*i);
        else if (spawnIt->second)
            data = sObjectMgr->GetCreatureData(spawnIt->second);

        if (!data)
            continue;

        Unit* Bm = PlayerbotAI::GetUnit(data);
        if (!Bm)
            continue;

        if (bot->GetMapId() != Bm->GetMapId())
            continue;

        // return first available guid on map if queue from anywhere
        if (!BattlegroundMgr::IsArenaType(bgTypeId))
        {
            battleMasterGUID = Bm->GetGUID();
            break;
        }

        AreaTableEntry const* zone = sAreaTableStore.LookupEntry(Bm->GetZoneId());
        if (!zone)
            continue;

        if (zone->team == 4 && bot->GetTeamId() == TEAM_ALLIANCE)
            continue;

        if (zone->team == 2 && bot->GetTeamId() == TEAM_HORDE)
            continue;

        if (Bm->getDeathState() == DeathState::Dead)
            continue;

        float dist2 = ServerFacade::instance().GetDistance2d(bot, data->posX, data->posY);
        if (dist2 < dist1)
        {
            dist1 = dist2;
            battleMasterGUID = Bm->GetGUID();
        }
    }

    return battleMasterGUID;
}
