/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaBossKnowledge.h"

#include "CellImpl.h"
#include "DatabaseEnv.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Log.h"
#include "Map.h"
#include "Multiplier.h"
#include "MovementActions.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Strategy.h"
#include "Timer.h"
#include "Trigger.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <initializer_list>
#include <list>
#include <set>
#include <unordered_map>

namespace CoaBossKnowledge
{
namespace
{
// Filled once by Load() before the world starts, read only afterwards: no lock needed.
std::unordered_map<uint32, std::vector<uint32>> knownSpells;
bool loaded = false;

uint8 Weight(Shape shape)
{
    switch (shape)
    {
        case Shape::PointBlank: return 5;
        case Shape::Cone: return 4;
        case Shape::AtDest: return 3;
        case Shape::Ground: return 2;
        case Shape::Chain: return 1;
        default: return 0;
    }
}

void Keep(Danger& best, Shape shape, float radius, float arc = 0.0f)
{
    if (Weight(shape) > Weight(best.shape) || (shape == best.shape && radius > best.radius))
    {
        best.shape = shape;
        best.radius = radius;
        best.arc = arc;
    }
}

float ConeArc(SpellInfo const* info, Targets target)
{
    if (SpellCone const* cone = sSpellMgr->GetSpellCone(info->Id))
        return float(cone->cone_degrees) * float(M_PI) / 180.0f;
    switch (target)
    {
        case TARGET_UNIT_CONE_ENEMY_24: return 24.0f * float(M_PI) / 180.0f;
        case TARGET_UNIT_CONE_ENEMY_54: return 54.0f * float(M_PI) / 180.0f;
        case TARGET_UNIT_CONE_ENEMY_104: return 104.0f * float(M_PI) / 180.0f;
        default: return 60.0f * float(M_PI) / 180.0f;
    }
}

Danger ClassifyDepth(SpellInfo const* info, uint8 depth)
{
    // No IsPositive() gate: a self aura that whirls (Whirlwind 8989) counts as positive.
    // The enemy checks on the targets below keep out what only helps the caster's side.
    Danger best;
    if (!info)
        return best;

    for (SpellEffectInfo const& effect : info->GetEffects())
    {
        if (!effect.IsEffect())
            continue;

        float const radius = effect.HasRadius() ? effect.CalcRadius() : 0.0f;

        if (effect.Effect == SPELL_EFFECT_PERSISTENT_AREA_AURA && !info->IsPositive())
            Keep(best, Shape::Ground, radius);

        for (SpellImplicitTargetInfo const& target : {effect.TargetA, effect.TargetB})
        {
            if (target.GetCheckType() != TARGET_CHECK_ENEMY)
                continue;
            switch (target.GetSelectionCategory())
            {
                case TARGET_SELECT_CATEGORY_CONE:
                    Keep(best, Shape::Cone, radius, ConeArc(info, target.GetTarget()));
                    break;
                case TARGET_SELECT_CATEGORY_AREA:
                {
                    bool const onCaster = target.GetReferenceType() == TARGET_REFERENCE_TYPE_CASTER ||
                                          effect.TargetA.GetTarget() == TARGET_SRC_CASTER ||
                                          effect.TargetA.GetTarget() == TARGET_DEST_CASTER;
                    Keep(best, onCaster ? Shape::PointBlank : Shape::AtDest, radius);
                    break;
                }
                default:
                    break;
            }
        }

        // Cleave and its kind jump to whoever stands next to the first target, in front.
        if (effect.ChainTarget > 1 && effect.TargetA.GetCheckType() == TARGET_CHECK_ENEMY)
        {
            if (info->DmgClass == SPELL_DAMAGE_CLASS_MELEE)
                Keep(best, Shape::Cone, 8.0f, float(M_PI));
            else
                Keep(best, Shape::Chain, info->JumpDistance > 0.0f ? info->JumpDistance : 10.0f);
        }

        // A channel such as Whirlwind: the aura casts the damaging spell every tick.
        if (depth == 0 && effect.Effect == SPELL_EFFECT_APPLY_AURA &&
            (effect.ApplyAuraName == SPELL_AURA_PERIODIC_TRIGGER_SPELL ||
             effect.ApplyAuraName == SPELL_AURA_PERIODIC_TRIGGER_SPELL_WITH_VALUE) &&
            effect.TriggerSpell)
        {
            Danger const tick = ClassifyDepth(sSpellMgr->GetSpellInfo(effect.TriggerSpell), depth + 1);
            if (tick.shape == Shape::PointBlank || tick.shape == Shape::Cone || tick.shape == Shape::AtDest)
                Keep(best, tick.shape, tick.radius, tick.arc);
        }
    }
    return best;
}
}  // namespace

Danger Classify(SpellInfo const* info) { return ClassifyDepth(info, 0); }

void Load()
{
    if (loaded)
        return;
    loaded = true;

    uint32 const oldMSTime = getMSTime();

    // 11 cast, 85 self cast, 86 cross cast, 134 invoker cast; 80/87/88 call timed action lists.
    QueryResult result = WorldDatabase.Query(
        "SELECT entryorguid, source_type, action_type, action_param1, action_param2, action_param3, "
        "action_param4, action_param5, action_param6 FROM smart_scripts "
        "WHERE source_type IN (0, 9) AND action_type IN (11, 85, 86, 134, 80, 87, 88)");
    if (!result)
        return;

    std::unordered_map<uint32, std::set<uint32>> creatureCasts, listCasts, creatureLists;
    do
    {
        Field* fields = result->Fetch();
        int32 const owner = fields[0].Get<int32>();
        uint8 const sourceType = fields[1].Get<uint8>();
        uint8 const action = fields[2].Get<uint8>();
        uint32 param[6];
        for (uint8 i = 0; i < 6; ++i)
            param[i] = fields[3 + i].Get<uint32>();
        if (owner <= 0)  // rows for one spawn (negative guid): left out of a per-entry table
            continue;

        if (action == 11 || action == 85 || action == 86 || action == 134)
            (sourceType == 0 ? creatureCasts : listCasts)[owner].insert(param[0]);
        else if (sourceType == 0 && action == 80)
            creatureLists[owner].insert(param[0]);
        else if (sourceType == 0 && action == 87)
        {
            for (uint32 list : param)
                if (list)
                    creatureLists[owner].insert(list);
        }
        else if (sourceType == 0 && action == 88 && param[1] >= param[0] && param[1] - param[0] < 50)
        {
            for (uint32 list = param[0]; list <= param[1]; ++list)
                creatureLists[owner].insert(list);
        }
    } while (result->NextRow());

    // Some creatures cast only through their timed action lists.
    for (auto const& [entry, lists] : creatureLists)
    {
        std::set<uint32>& casts = creatureCasts[entry];
        for (uint32 list : lists)
        {
            auto const found = listCasts.find(list);
            if (found != listCasts.end())
                casts.insert(found->second.begin(), found->second.end());
        }
    }

    uint32 spells = 0;
    for (auto const& [entry, casts] : creatureCasts)
    {
        if (casts.empty())
            continue;
        knownSpells[entry].assign(casts.begin(), casts.end());
        spells += casts.size();
    }

    LOG_INFO("server.loading", ">> CoA boss knowledge: {} creature casts read for {} creatures in {} ms", spells,
             knownSpells.size(), GetMSTimeDiffToNow(oldMSTime));
}

std::vector<uint32> const* KnownSpells(uint32 creatureEntry)
{
    auto const found = knownSpells.find(creatureEntry);
    return found == knownSpells.end() ? nullptr : &found->second;
}

float InstantPointBlankRadius(uint32 creatureEntry)
{
    float widest = 0.0f;
    if (std::vector<uint32> const* spells = KnownSpells(creatureEntry))
    {
        for (uint32 spellId : *spells)
        {
            SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
            if (!info || info->CalcCastTime() > 0 || info->IsChanneled())
                continue;
            Danger const danger = Classify(info);
            if (danger.shape == Shape::PointBlank && danger.radius <= sPlayerbotAIConfig.maxAoeAvoidRadius)
                widest = std::max(widest, danger.radius);
        }
    }
    return widest;
}
}  // namespace CoaBossKnowledge

namespace
{
using namespace CoaBossKnowledge;

Spell const* CurrentCast(Unit* unit)
{
    if (Spell const* channel = unit->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
        return channel;
    return unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
}

// The enemy whose casts the bot watches first: its own target, inside a dungeon or a raid.
Unit* WatchedEnemy(PlayerbotAI* botAI, Player* bot)
{
    if (!bot->IsInCombat() || !bot->GetMap() || !bot->GetMap()->IsDungeon())
        return nullptr;
    Unit* enemy = botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
    if (!enemy || !enemy->IsAlive() || enemy->GetTypeId() != TYPEID_UNIT)
        return nullptr;
    return enemy;
}

// A point blank cast running on a hostile creature other than `except`, boss or not, whose area takes in `at`:
// with every bot on an add, nobody watched Rhahk'zor and his Bladestorm (2102554, 8 yards a tick) hit the whole
// group (headless dungeon runs, 11/10). The caster whose area reaches furthest past `at`, nullptr if none does.
Unit* PointBlankCasterOver(Player* bot, Position const& at, Unit* except, float& area, SpellInfo const*& cast)
{
    if (!bot->IsInCombat() || !bot->GetMap() || !bot->GetMap()->IsDungeon())
        return nullptr;
    float const range = sPlayerbotAIConfig.maxAoeAvoidRadius + 10.0f + bot->GetExactDist2d(&at);
    std::list<Unit*> units;
    Acore::AnyUnfriendlyUnitInObjectRangeCheck check(bot, bot, range);
    Acore::UnitListSearcher<Acore::AnyUnfriendlyUnitInObjectRangeCheck> searcher(bot, units, check);
    Cell::VisitObjects(bot, searcher, range);

    Unit* found = nullptr;
    float deepest = 0.0f;
    for (Unit* unit : units)
    {
        if (unit == except || !unit->IsAlive() || unit->GetTypeId() != TYPEID_UNIT)
            continue;
        Spell const* spell = CurrentCast(unit);
        if (!spell)
            continue;
        Danger const danger = Classify(spell->GetSpellInfo());
        if (danger.shape != Shape::PointBlank || danger.radius <= 0.0f ||
            danger.radius > sPlayerbotAIConfig.maxAoeAvoidRadius)
            continue;
        float const reach = danger.radius + unit->GetCombatReach() + 1.0f;
        float const depth = reach - unit->GetExactDist2d(&at);
        if (depth > deepest)
        {
            deepest = depth;
            found = unit;
            area = reach;
            cast = spell->GetSpellInfo();
        }
    }
    return found;
}

class CoaBossDangerTrigger : public Trigger
{
public:
    CoaBossDangerTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa boss danger") {}

    bool IsActive() override
    {
        Unit* enemy = WatchedEnemy(botAI, bot);
        if (enemy && CurrentCast(enemy))
            return true;
        float area = 0.0f;
        SpellInfo const* cast = nullptr;
        if (PointBlankCasterOver(bot, bot->GetPosition(), enemy, area, cast))
            return true;
        if (!enemy || !PlayerbotAI::IsRanged(bot) || PlayerbotAI::IsTank(bot))
            return false;
        uint32 const entry = enemy->GetEntry();
        if (entry != lastEntry)
        {
            lastEntry = entry;
            instantRadius = InstantPointBlankRadius(entry);
        }
        return instantRadius > 0.0f &&
               bot->GetExactDist2d(enemy) < instantRadius + enemy->GetCombatReach() + 1.0f;
    }

private:
    uint32 lastEntry = 0;
    float instantRadius = 0.0f;
};

class CoaBossDodgeAction : public MovementAction
{
public:
    CoaBossDodgeAction(PlayerbotAI* botAI) : MovementAction(botAI, "coa boss dodge") {}

    // The trigger only says the enemy casts something; isUseful() keeps the casts that matter here.
    bool isUseful() override { return Act(true); }
    bool Execute(Event /*event*/) override { return Act(false); }

private:
    bool Act(bool dryRun)
    {
        Unit* enemy = WatchedEnemy(botAI, bot);
        if (enemy && ActOn(enemy, dryRun))
            return true;

        // Another enemy than the bot's own: the tank it beats on stays where it is.
        float area = 0.0f;
        SpellInfo const* cast = nullptr;
        Unit* caster = PointBlankCasterOver(bot, bot->GetPosition(), enemy, area, cast);
        if (!caster || (PlayerbotAI::IsTank(bot) && caster->GetVictim() == bot))
            return false;
        return dryRun || Dodge(caster->GetPosition(), area, cast);
    }

    bool ActOn(Unit* enemy, bool dryRun)
    {

        bool const tank = PlayerbotAI::IsTank(bot);
        float const reach = enemy->GetCombatReach() + 1.0f;
        float const distance = bot->GetExactDist2d(enemy);

        if (Spell const* spell = CurrentCast(enemy))
        {
            SpellInfo const* info = spell->GetSpellInfo();
            Danger const danger = Classify(info);
            if (danger.radius <= 0.0f || danger.radius > sPlayerbotAIConfig.maxAoeAvoidRadius)
                return false;

            switch (danger.shape)
            {
                case Shape::PointBlank:
                    if (!tank && distance < danger.radius + reach)
                        return dryRun || Dodge(enemy->GetPosition(), danger.radius + reach, info);
                    return false;
                case Shape::Cone:
                    if (!tank && distance < danger.radius + reach && enemy->isInFront(bot, danger.arc + 0.5f))
                        return dryRun || SideStep(enemy, danger.arc, info);
                    // within the bot's boundary (at least 2 yd) a cone hits at any angle, behind the caster too
                    if (!tank && bot->GetExactDist(enemy) < std::max(bot->GetBoundaryRadius(), MIN_MELEE_REACH) + 0.5f)
                        return dryRun || Dodge(enemy->GetPosition(), std::max(bot->GetBoundaryRadius(), MIN_MELEE_REACH) + 1.0f, info);
                    return false;
                case Shape::AtDest:
                    if (spell->m_targets.HasDst())
                    {
                        WorldLocation const* spot = spell->m_targets.GetDstPos();
                        if (bot->GetExactDist2d(spot) < danger.radius + 1.0f)
                            return dryRun || Dodge(*spot, danger.radius + 1.0f, info);
                    }
                    return false;
                default:
                    return false;
            }
        }

        if (tank || !PlayerbotAI::IsRanged(bot))
            return false;
        float const instant = InstantPointBlankRadius(enemy->GetEntry());
        if (instant > 0.0f && distance < instant + reach)
            return dryRun || Dodge(enemy->GetPosition(), instant + reach, nullptr);
        return false;
    }

    bool Dodge(Position const& from, float radius, SpellInfo const* info)
    {
        if (!FleePosition(from, radius))
            return false;
        Tell(info);
        return true;
    }

    // Out of the cone, to whichever side of the caster is nearer, at the same distance.
    bool SideStep(Unit* enemy, float arc, SpellInfo const* info)
    {
        float const facing = enemy->GetOrientation();
        float const current = enemy->GetAngle(bot);
        float const away = arc / 2.0f + 0.6f;
        float const left = Position::NormalizeOrientation(facing + away);
        float const right = Position::NormalizeOrientation(facing - away);
        auto gap = [current](float angle)
        {
            float delta = std::fabs(Position::NormalizeOrientation(angle - current));
            return delta > float(M_PI) ? 2.0f * float(M_PI) - delta : delta;
        };
        float const angle = gap(left) < gap(right) ? left : right;
        float const distance = std::max(bot->GetExactDist2d(enemy), enemy->GetCombatReach() + 1.5f);

        float x = enemy->GetPositionX() + std::cos(angle) * distance;
        float y = enemy->GetPositionY() + std::sin(angle) * distance;
        float z = bot->GetPositionZ();
        if (!bot->GetMap()->CheckCollisionAndGetValidCoords(bot, bot->GetPositionX(), bot->GetPositionY(),
                                                            bot->GetPositionZ(), x, y, z))
            return false;
        if (!MoveTo(bot->GetMapId(), x, y, z, false, false, false, false, MovementPriority::MOVEMENT_COMBAT))
            return false;
        Tell(info);
        return true;
    }

    void Tell(SpellInfo const* info)
    {
        if (!sPlayerbotAIConfig.tellWhenAvoidAoe || lastTell > time(nullptr) - 10)
            return;
        lastTell = time(nullptr);
        std::string text = "Out of the way of ";
        text += info ? info->SpellName[0] : "the boss";
        bot->Say(text, LANG_UNIVERSAL);
    }

    time_t lastTell = 0;
};

// While a point blank cast runs, a melee bot that stepped out would walk straight back in to
// reach melee range or get behind: those moves wait until the cast ends, that of its target or
// that of another enemy whose area takes in its target.
class CoaBossDodgeMultiplier : public Multiplier
{
public:
    CoaBossDodgeMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "coa boss dodge") {}

    float GetValue(Action* action) override
    {
        if (!action)
            return 1.0f;
        std::string const name = action->getName();
        if (name != "reach melee" && name != "set behind")
            return 1.0f;
        if (PlayerbotAI::IsTank(bot))
            return 1.0f;
        Unit* enemy = WatchedEnemy(botAI, bot);
        if (Spell const* spell = enemy ? CurrentCast(enemy) : nullptr)
        {
            Danger const danger = Classify(spell->GetSpellInfo());
            if (danger.shape == Shape::PointBlank && danger.radius > 0.0f &&
                danger.radius <= sPlayerbotAIConfig.maxAoeAvoidRadius)
                return 0.0f;
        }
        // Nor does it walk back to its target inside another enemy's point blank cast.
        float area = 0.0f;
        SpellInfo const* cast = nullptr;
        return enemy && PointBlankCasterOver(bot, enemy->GetPosition(), enemy, area, cast) ? 0.0f : 1.0f;
    }
};

class CoaBossDodgeStrategy : public Strategy
{
public:
    CoaBossDodgeStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}

    std::string const getName() override { return "coa boss dodge"; }

    void InitTriggers(std::vector<TriggerNode*>& triggers) override
    {
        // Just under upstream "avoid aoe" (ACTION_EMERGENCY): standing in a ground effect comes first.
        triggers.push_back(new TriggerNode("coa boss danger", { NextAction("coa boss dodge", ACTION_EMERGENCY - 1.0f) }));
    }

    void InitMultipliers(std::vector<Multiplier*>& multipliers) override
    {
        multipliers.push_back(new CoaBossDodgeMultiplier(botAI));
    }
};
}  // namespace

Strategy* NewCoaBossDodgeStrategy(PlayerbotAI* botAI) { return new CoaBossDodgeStrategy(botAI); }
Action* NewCoaBossDodgeAction(PlayerbotAI* botAI) { return new CoaBossDodgeAction(botAI); }
Trigger* NewCoaBossDangerTrigger(PlayerbotAI* botAI) { return new CoaBossDangerTrigger(botAI); }
