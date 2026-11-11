#include "CoaCatchUp.h"

#include "AsyncCallbackProcessor.h"
#include "CoaFreshStartRules.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "QueryCallback.h"
#include "StringFormat.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <ctime>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace CoaCatchUp
{
namespace
{
constexpr uint64 SURVEY_MS = 10 * MINUTE * IN_MILLISECONDS;
constexpr int64 RECORD_S = HOUR;
constexpr size_t RECORDS_KEPT = 72;
constexpr uint32 MAX_LEVEL_SEEN = 100;

QueryCallbackProcessor callbacks;  // world thread only
bool surveying = false;
uint64 nextSurveyMs = 0;
std::atomic<uint32> front{0};

std::mutex lock;  // what follows: written by the survey (world thread), read by world.json
uint32 humans = 0;
int64 surveyedAt = 0;
std::vector<uint32> botLevels;  // random bots by level (all of them, online or not)
struct Record
{
    int64 at;
    uint32 front, humans, bots, p10, p50, p90;
};
std::deque<Record> records;
int64 nextRecord = 0;

CoaFreshStart::CatchUpSettings Settings()
{
    CoaFreshStart::CatchUpSettings s;
    s.marginLevels = sPlayerbotAIConfig.coaCatchUpMarginLevels;
    s.stepPercent = sPlayerbotAIConfig.coaCatchUpStepPercent;
    s.maxRatePercent = uint32(std::max(1.0f, sPlayerbotAIConfig.coaCatchUpMaxRate) * 100.0f + 0.5f);
    s.minHumans = sPlayerbotAIConfig.coaCatchUpMinHumans;
    s.maxTotalPercent = uint32(std::max(0.0f, sPlayerbotAIConfig.coaCatchUpMaxTotalRate) * 100.0f + 0.5f);
    return s;
}

void ReadCharacters(std::unordered_set<uint32> const& humanAccounts, QueryResult result)
{
    surveying = false;
    int64 const now = int64(time(nullptr));
    int64 const since = now - int64(sPlayerbotAIConfig.coaCatchUpActiveDays) * DAY;
    std::unordered_map<uint32, uint32> best;  // active human account -> its highest level
    std::vector<uint32> bots(MAX_LEVEL_SEEN + 1, 0);
    if (result)
    {
        do
        {
            Field* f = result->Fetch();
            uint32 const account = f[0].Get<uint32>();
            uint32 const level = std::min<uint32>(f[1].Get<uint8>(), MAX_LEVEL_SEEN);
            bool const active = f[2].Get<uint8>() || int64(f[3].Get<uint32>()) >= since;
            if (humanAccounts.count(account))
            {
                if (active)
                    best[account] = std::max(best[account], level);
            }
            else if (sPlayerbotAIConfig.IsInRandomAccountList(account))
                ++bots[level];
        } while (result->NextRow());
    }
    std::vector<uint32> levels;
    levels.reserve(best.size());
    for (auto const& [account, level] : best)
        levels.push_back(level);
    uint32 const f =
        sPlayerbotAIConfig.coaCatchUpEnable ? CoaFreshStart::MedianFront(levels, sPlayerbotAIConfig.coaCatchUpMinHumans) : 0;
    front = f;

    std::lock_guard<std::mutex> guard(lock);
    humans = uint32(levels.size());
    surveyedAt = now;
    botLevels = std::move(bots);
    if (now >= nextRecord)
    {
        nextRecord = now + RECORD_S;
        uint32 count = 0;
        for (uint32 c : botLevels)
            count += c;
        records.push_back({now, f, humans, count, CoaFreshStart::LevelAtPercentile(botLevels, 10),
                           CoaFreshStart::LevelAtPercentile(botLevels, 50),
                           CoaFreshStart::LevelAtPercentile(botLevels, 90)});
        while (records.size() > RECORDS_KEPT)
            records.pop_front();
    }
}

// Human accounts: not a bot account (the random bot prefix, the auction house), no GM rights; then the levels.
void StartSurvey()
{
    surveying = true;
    std::string prefix = sPlayerbotAIConfig.randomBotAccountPrefix;
    prefix.erase(std::remove_if(prefix.begin(), prefix.end(), [](char c) { return !std::isalnum(uint8(c)); }),
                 prefix.end());
    std::string const query = Acore::StringFormat(
        "SELECT a.id FROM account a WHERE a.username NOT LIKE '{}%' AND a.username <> 'AHBOT' AND NOT EXISTS "
        "(SELECT 1 FROM account_access aa WHERE aa.id = a.id AND aa.gmlevel > 0)",
        prefix.empty() ? std::string("rndbot") : prefix);
    callbacks.AddCallback(LoginDatabase.AsyncQuery(query).WithCallback(
        [](QueryResult accounts)
        {
            std::unordered_set<uint32> human;
            if (accounts)
            {
                do
                {
                    uint32 const id = accounts->Fetch()[0].Get<uint32>();
                    if (!sPlayerbotAIConfig.IsInRandomAccountList(id))
                        human.insert(id);
                } while (accounts->NextRow());
            }
            callbacks.AddCallback(
                CharacterDatabase.AsyncQuery("SELECT account, level, online, logout_time FROM characters WHERE deleteDate IS NULL")
                    .WithCallback([human = std::move(human)](QueryResult levels) { ReadCharacters(human, std::move(levels)); }));
        }));
}
}  // namespace

void Tick(uint64 nowMs)
{
    callbacks.ProcessReadyCallbacks();
    if (!sPlayerbotAIConfig.coaFreshStartEnable)
    {
        front = 0;
        return;
    }
    if (surveying || nowMs < nextSurveyMs)
        return;
    nextSurveyMs = nowMs + SURVEY_MS;
    StartSurvey();
}

uint32 Front() { return front.load(); }

uint32 XpPercent(Player* bot)
{
    uint32 const baseRate = uint32(std::max(0.0f, sPlayerbotAIConfig.randomBotXPRate) * 100.0f + 0.5f);
    return CoaFreshStart::FreshStartPercent(bot->GetGUID().GetCounter(), bot->GetLevel(), Front(),
                                            sPlayerbotAIConfig.coaCatchUpEnable, Settings(), baseRate);
}

std::string Json()
{
    if (!sPlayerbotAIConfig.coaFreshStartEnable)
        return "";
    std::lock_guard<std::mutex> guard(lock);
    std::string out = Acore::StringFormat("{{\"catchUp\":{},\"front\":{},\"humans\":{},\"surveyedAt\":{},\"bots\":[",
                                          sPlayerbotAIConfig.coaCatchUpEnable ? "true" : "false", Front(), humans,
                                          surveyedAt);
    // "bots": the random bots by level, from level 1 to the highest held (index 0 = level 1).
    size_t last = botLevels.size();
    while (last && !botLevels[last - 1])
        --last;
    for (size_t i = 1; i < last; ++i)
        out += (i > 1 ? "," : "") + std::to_string(botLevels[i]);
    out += "],\"hourly\":[";
    bool first = true;
    for (Record const& r : records)
    {
        out += Acore::StringFormat("{}{{\"at\":{},\"front\":{},\"humans\":{},\"bots\":{},\"p10\":{},\"p50\":{},\"p90\":{}}}",
                                   first ? "" : ",", r.at, r.front, r.humans, r.bots, r.p10, r.p50, r.p90);
        first = false;
    }
    return out + "]}";
}
}  // namespace CoaCatchUp
