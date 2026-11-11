/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaLaunchCurve.h"

#include "CoaEconomyConf.h"
#include "CoaEconomyRules.h"

#include "AccountMgr.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "World.h"

#include <algorithm>
#include <map>

namespace CoaEconomy
{

namespace
{
    // The house bot's automatic account (see CoaAuctionHouseBot.cpp): its characters are not population.
    constexpr char const* HouseAccountName = "AHBOT";
}

LaunchCurve* LaunchCurve::instance()
{
    static LaunchCurve instance;
    return &instance;
}

void LaunchCurve::LoadSettings()
{
    _enabled = CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.LaunchCurve.Enable", false, false);
    _refreshMs = std::max<uint32>(1, CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.LaunchCurve.RefreshMinutes", 10)) * MINUTE * IN_MILLISECONDS;
    _frontPercentile = std::clamp<uint32>(CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.LaunchCurve.FrontPercentile", 80), 1, 100);
    _topCount = std::max<uint32>(1, CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.LaunchCurve.TopCount", 3));
    _minPopulation = CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.LaunchCurve.MinPopulation", 20);
    _levelMargin = CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.LaunchCurve.LevelMargin", 3);
    _rareFromLevel = CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.LaunchCurve.RareFromLevel", 20);
    _epicFromLevel = CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.LaunchCurve.EpicFromLevel", 50);
    _allowLegendary = CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.LaunchCurve.AllowLegendary", false);
    _startStock = std::clamp(CoaEconomy::Conf::Get<float>("AiPlayerbot.CoaEconomy.LaunchCurve.StartStockPercent", 10.0f) / 100.0f, 0.0f, 1.0f);
    _launchPremium = std::max(0.0f, CoaEconomy::Conf::Get<float>("AiPlayerbot.CoaEconomy.LaunchCurve.LaunchPremiumPercent", 150.0f) / 100.0f);
    // A changed setting is taken into account at the next survey.
    _timer = std::min(_timer, _refreshMs);
}

void LaunchCurve::Update(uint32 diff)
{
    _callbacks.ProcessReadyCallbacks();
    if (!_enabled || _surveying)
        return;

    if (_timer > diff)
    {
        _timer -= diff;
        return;
    }
    _timer = _refreshMs;
    StartSurvey();
}

// Two async queries, never on the world tick: GM accounts (they are not population), then the levels.
void LaunchCurve::StartSurvey()
{
    _surveying = true;
    _callbacks.AddCallback(LoginDatabase.AsyncQuery("SELECT DISTINCT id FROM account_access WHERE gmlevel > 0").WithCallback(
        [this](QueryResult gmAccounts)
        {
            _excludedAccounts.clear();
            if (gmAccounts)
            {
                do
                    _excludedAccounts.insert(gmAccounts->Fetch()[0].Get<uint32>());
                while (gmAccounts->NextRow());
            }
            if (uint32 houseAccount = AccountMgr::GetId(HouseAccountName))
                _excludedAccounts.insert(houseAccount);

            _callbacks.AddCallback(CharacterDatabase.AsyncQuery("SELECT account, level FROM characters WHERE deleteDate IS NULL")
                .WithCallback([this](QueryResult levels) { ReadLevels(std::move(levels)); }));
        }));
}

void LaunchCurve::ReadLevels(QueryResult result)
{
    _surveying = false;

    // level -> characters at that level, players' alts and bots alike, GMs and the house left out
    std::map<uint32, uint32> byLevel;
    uint32 population = 0;
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            if (_excludedAccounts.count(fields[0].Get<uint32>()))
                continue;
            ++byLevel[fields[1].Get<uint8>()];
            ++population;
        } while (result->NextRow());
    }

    uint32 const maxLevel = std::max<uint32>(1, sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL));
    _population = population;
    _surveyed = true;
    if (population < _minPopulation || byLevel.empty())
    {
        _frontLevel = 1;
        _topLevel = 1;
        _progress = 0.0f;
        return;
    }

    // Front: the level FrontPercentile % of the population has reached at most; top: the highest level that at least
    // TopCount characters have reached.
    _frontLevel = CoaEconomyRules::FrontLevel(byLevel, _frontPercentile);
    _topLevel = CoaEconomyRules::TopLevel(byLevel, _topCount);

    _progress = std::clamp(float(_frontLevel) / float(maxLevel), 0.0f, 1.0f);
}

bool LaunchCurve::AllowsListing(ItemTemplate const& proto) const
{
    if (!_enabled)
        return true;

    CoaEconomyRules::LaunchCaps caps;
    caps.front = _frontLevel;
    caps.top = _topLevel;
    caps.levelMargin = _levelMargin;
    caps.rareFromLevel = _rareFromLevel;
    caps.epicFromLevel = _epicFromLevel;
    caps.allowLegendary = _allowLegendary;
    return CoaEconomyRules::LaunchAllows(caps, proto.RequiredLevel, proto.ItemLevel, proto.Quality);
}

float LaunchCurve::StockFactor() const
{
    if (!_enabled)
        return 1.0f;
    return CoaEconomyRules::LaunchStockFactor(_startStock, _progress);
}

float LaunchCurve::PriceFactor() const
{
    if (!_enabled)
        return 1.0f;
    return CoaEconomyRules::LaunchPriceFactor(_launchPremium, _progress);
}

} // namespace CoaEconomy

namespace
{
    class CoaLaunchCurveWorld : public WorldScript
    {
    public:
        CoaLaunchCurveWorld() : WorldScript("CoaLaunchCurveWorld", {WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_UPDATE}) { }

        void OnAfterConfigLoad(bool /*reload*/) override { sCoaLaunchCurve->LoadSettings(); }
        void OnUpdate(uint32 diff) override { sCoaLaunchCurve->Update(diff); }
    };
}

void AddSC_coa_economy_launch()
{
    // Off by default; read at start-up (restart to change it). The settings below it are read again on reload.
    if (!CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.LaunchCurve.Enable", false, false))
        return;

    sCoaLaunchCurve->LoadSettings();
    new CoaLaunchCurveWorld();
}
