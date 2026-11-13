/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_ECONOMY_LAUNCH_CURVE_H
#define COA_ECONOMY_LAUNCH_CURVE_H

#include "AsyncCallbackProcessor.h"
#include "Define.h"
#include "QueryCallback.h"

#include <unordered_set>

struct ItemTemplate;

namespace CoaEconomy
{

// Launch auction house of a new realm (AiPlayerbot.CoaEconomy.LaunchCurve.*, off by default; design in
// docs/LAUNCHCURVE-3.0.md). The house bot lists what the population could really have reached: a level
// and quality cap that follows the realm's characters, a stock that grows, and a launch price premium
// that fades. The signal is the population, not the calendar: a realm that is off does not progress.
class LaunchCurve
{
public:
    static LaunchCurve* instance();

    void LoadSettings();
    bool IsEnabled() const { return _enabled; }

    // World thread: runs the ready query callbacks and starts a new survey when it is due.
    void Update(uint32 diff);

    // Whether the house may list this item now.
    bool AllowsListing(ItemTemplate const& proto) const;
    // Whether one item may cost this much now (copper): UnitPriceCapGold x front^2 gold until UnitPriceCapUntilLevel.
    bool AllowsUnitPrice(uint64 copper) const;
    // Share of the house's MinItems/MaxItems to use now (StartStockPercent .. 1).
    float StockFactor() const;
    // Launch price premium (1 .. 1 + LaunchPremiumPercent/100), growing with nothing but time-to-progress.
    float PriceFactor() const;

    uint32 GetFrontLevel() const { return _frontLevel; }
    uint32 GetTopLevel() const { return _topLevel; }
    float GetProgress() const { return _progress; }
    uint32 GetPopulation() const { return _population; }

private:
    LaunchCurve() = default;

    void StartSurvey();
    void ReadLevels(QueryResult result);

    bool _enabled = false;
    uint32 _refreshMs = 10 * 60 * 1000;
    uint32 _frontPercentile = 80;
    uint32 _topCount = 3;
    uint32 _minPopulation = 20;
    uint32 _levelMargin = 3;
    uint32 _rareFromLevel = 20;
    uint32 _epicFromLevel = 50;
    bool _allowLegendary = false;
    float _startStock = 0.10f;
    float _launchPremium = 1.50f;
    float _unitPriceCapGold = 0.25f;
    uint32 _unitPriceCapUntilLevel = 60;

    // Survey state: starts at the very beginning of a realm until the first survey answers.
    uint32 _timer = 0;
    bool _surveying = false;
    bool _surveyed = false;
    uint32 _frontLevel = 1;
    uint32 _topLevel = 1;
    uint32 _population = 0;
    float _progress = 0.0f;
    std::unordered_set<uint32> _excludedAccounts;
    std::unordered_set<uint32> _gmAccounts;
    QueryCallbackProcessor _callbacks;
};

} // namespace CoaEconomy

#define sCoaLaunchCurve CoaEconomy::LaunchCurve::instance()

#endif
