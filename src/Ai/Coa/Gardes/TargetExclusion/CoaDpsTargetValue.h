/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_DPS_TARGET_VALUE_H
#define COA_DPS_TARGET_VALUE_H

#include <string>

#include "DpsTargetValue.h"
#include "Strategy.h"
#include "Value.h"

class PlayerbotAI;
class Unit;

// THE HOLE the exclusion table cannot close on its own (moved from Dungeon Clear's DungeonClearDpsTargetValue, logic
// unchanged). The exclusions reach the stock engine through Strategy::AppendTargetExclusions, which drops a barred
// guid from the attacker sweep, but DpsTargetValue returns the RAID ICON target before the sweep is read: on
// Razorgore, `bwl razorgore mark boss` paints the one creature whose phase-1 death instakills the raid.
//
// So these front the stock `dps target` / `dps aoe target` by name (the module's guard context is added after the
// base context and the creator map is last-wins), delegate to the stock pick unchanged, and intervene in one case
// only: the stock answer is barred for this bot right now. Then they re-run the SAME stock pick with the raid icon
// disarmed (built against "coa no rti", an icon name that always reads ""), so what comes back is the attacker sweep's
// answer with the exclusions applied, or nothing. The icon itself is untouched: it is how the off-tank knows to pick
// the boss up. Tanks read `tank target` and are untouched here.
inline constexpr char const* CoaNoRtiKey = "coa no rti";

class CoaNoRtiValue : public CalculatedValue<std::string>
{
public:
    CoaNoRtiValue(PlayerbotAI* botAI) : CalculatedValue<std::string>(botAI, CoaNoRtiKey) {}
    std::string Calculate() override { return ""; }
};

class CoaDpsTargetValue : public DpsTargetValue
{
public:
    CoaDpsTargetValue(PlayerbotAI* botAI)
        : DpsTargetValue(botAI, "rti", "dps target"), unmarked(botAI, CoaNoRtiKey, "coa dps target unmarked")
    {
    }

    Unit* Calculate() override;

private:
    DpsTargetValue unmarked;  // the same picker with the raid-icon short-circuit disarmed
};

class CoaDpsAoeTargetValue : public DpsAoeTargetValue
{
public:
    CoaDpsAoeTargetValue(PlayerbotAI* botAI)
        : DpsAoeTargetValue(botAI, "rti", "dps aoe target"), unmarked(botAI, CoaNoRtiKey, "coa dps aoe target unmarked")
    {
    }

    Unit* Calculate() override;

private:
    DpsAoeTargetValue unmarked;
};

// Carries the exclusions into the stock engine's own target selection (attacker sweep, debuff pools, AoE counts, and
// the tank pool for `alsoTank` rows). COMBAT engine. Ported from DungeonClearCombatStrategy's pair, logic unchanged.
class CoaTargetExclusionStrategy : public Strategy
{
public:
    CoaTargetExclusionStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}
    std::string const getName() override { return "coa target exclusion"; }
    bool HasTargetExclusions() const override;
    void AppendTargetExclusions(GuidSet& exclusions, TargetValueExclusionType type) override;
};

#endif
