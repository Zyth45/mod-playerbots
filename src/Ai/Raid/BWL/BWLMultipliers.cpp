/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "BWLMultipliers.h"
#include "BWLActions.h"
#include "BWLHelpers.h"
#include "ChooseTargetActions.h"
#include "GenericSpellActions.h"
#include "HunterActions.h"
#include "ObjectAccessor.h"
#include "Playerbots.h"
#include "ReachTargetActions.h"

using namespace BlackwingLairHelpers;

static constexpr float VAELASTRASZ_BA_SAFE_DISTANCE = 20.0f;
static constexpr float VAELASTRASZ_BA_BOSS_DISTANCE = 30.0f;

float RazorgorePossessionMultiplier::GetValue(Action* action)
{
    // Read off the bot's own charm field, never a cross-bot signal: the channel is what must not break, so the
    // channel decides. A warlock's enslaved demon is a charm too, hence the entry test.
    constexpr uint32 NPC_RAZORGORE_THE_UNTAMED = 12435;
    ObjectGuid const charm = bot->GetCharmGUID();
    if (charm.IsEmpty())
        return 1.0f;
    Unit* held = ObjectAccessor::GetUnit(*bot, charm);
    if (!held || !held->IsAlive() || held->GetEntry() != NPC_RAZORGORE_THE_UNTAMED)
        return 1.0f;

    // The one action left: Dungeon Clear's orb rung, which stops the bot's swing and movement while it holds him.
    return action && action->getName() == "dungeon clear razorgore orb" ? 1.0f : 0.0f;
}

float RazorgoreTankMultiplier::GetValue(Action* action)
{
    if (!PlayerbotAI::IsTank(bot))
        return 1.0f;

    Unit* boss = AI_VALUE2(Unit*, "find target", "razorgore the untamed");
    if (!boss)
        return 1.0f;

    if (AreRazorgoreEggsAlive(botAI))
    {
        // Off-tank picks up boss, blocks TankAssistAction to avoid changing targets
        if (IsRazorgoreOffTank(bot) && bot->GetVictim() != nullptr && dynamic_cast<TankAssistAction*>(action))
            return 0.0f;
        return 1.0f;
    }

    // All eggs destroyed. Tanks not targeted by the boss don't run into the Cleave.
    if (boss->GetVictim() != bot && dynamic_cast<TankFaceAction*>(action))
        return 0.0f;
    return 1.0f;
}

float VaelastraszTankMultiplier::GetValue(Action* action)
{
    if (!PlayerbotAI::IsTank(bot))
        return 1.0f;

    Unit* boss = AI_VALUE2(Unit*, "find target", "vaelastrasz the corrupt");
    if (!boss)
        return 1.0f;

    // Stay at rear flank position and don't move in front of the boss.
    if (dynamic_cast<TankFaceAction*>(action))
    {
        Unit* victim = boss->GetVictim();
        Player* victimPlayer = victim ? victim->ToPlayer() : nullptr;
        if (victim != bot && victimPlayer && PlayerbotAI::IsTank(victimPlayer))
            return 0.0f;
    }

    return 1.0f;
}

float VaelastraszBurningAdrenalineMultiplier::GetValue(Action* action)
{
    if (HasBurningAdrenaline(bot))
    {
        if (dynamic_cast<MovementAction*>(action))
        {
            if (dynamic_cast<BwlVaelastraszMoveAwayAction*>(action))
            {
                // Allow movement when boss is dead or bot was knocked away
                Unit* boss = AI_VALUE2(Unit*, "find target", "vaelastrasz the corrupt");
                if (!boss || !boss->IsAlive() || bot->GetDistance2d(boss) > VAELASTRASZ_BA_BOSS_DISTANCE)
                    return 1.0f;
            }

            // Block ALL other movement once bot is far enough from non-BA bots
            if (!IsNonBABotNearPosition(bot, bot->GetPosition(), VAELASTRASZ_BA_SAFE_DISTANCE) ||
                !dynamic_cast<BwlVaelastraszMoveAwayAction*>(action))
                return 0.0f;
        }
        // Also block charge actions
        if (dynamic_cast<CastReachTargetSpellAction*>(action))
            return 0.0f;

        // A tank under it generates no threat and leaves him to the next tank: no taunting him back.
        std::string const name = action->getName();
        if (name == "coa taunt" || name == "taunt spell" || name == "taunt" || name == "growl")
        {
            Unit* boss = AI_VALUE2(Unit*, "find target", "vaelastrasz the corrupt");
            if (boss && boss->IsAlive() && boss->GetVictim() != bot)
                return 0.0f;
        }
    }

    return 1.0f;
}

// ---- CoA Combat (lot 7, docs/BWL-ETAT-3.0.md) ----

namespace
{
    bool IsTauntAction(Action* action)
    {
        std::string const name = action->getName();
        return name == "taunt spell" || name == "taunt" || name == "growl" || name == "hand of reckoning" ||
               name == "dark command" || name == "righteous defense" || name == "challenging shout" ||
               name == "challenging roar" || name == "mocking blow";
    }

    bool IsAreaOrCleaveAction(Action* action)
    {
        std::string const name = action->getName();
        if (name == "cleave" || name == "whirlwind" || name == "blade flurry" || name == "multi-shot" ||
            name == "swipe" || name == "swipe (bear)" || name == "consecration" || name == "thunder clap" ||
            name == "volley" || name == "blizzard" || name == "flamestrike" || name == "arcane explosion" ||
            name == "hurricane" || name == "rain of fire" || name == "hellfire" || name == "chain lightning" ||
            name == "death and decay" || name == "blood boil" || name == "howling blast" || name == "fan of knives" ||
            name == "shockwave" || name == "cone of cold" || name == "frost nova" || name == "holy nova")
            return true;
        if (auto castSpellAction = dynamic_cast<CastSpellAction*>(action))
            return castSpellAction->getThreatType() == Action::ActionThreatType::Aoe;
        return false;
    }
}

float BwlDrakeTauntHoldMultiplier::GetValue(Action* action)
{
    if (!BwlCoaFixesOn() || !PlayerbotAI::IsTank(bot) || !IsTauntAction(action))
        return 1.0f;
    for (bool firemaw : {true, false})
    {
        Unit* boss = AI_VALUE2(Unit*, "find target", firemaw ? "firemaw" : "ebonroc");
        if (!boss || !boss->IsAlive())
            continue;
        Unit* victim = boss->GetVictim();
        Player* victimPlayer = victim ? victim->ToPlayer() : nullptr;
        if (victimPlayer && victim != bot && PlayerbotAI::IsTank(victimPlayer) && !CanTakeDrake(bot, firemaw))
            return 0.0f;
    }
    return 1.0f;
}

float BwlDominionCloneAoeMultiplier::GetValue(Action* action)
{
    constexpr float CLONE_RANGE = 10.0f;
    if (!BwlCoaFixesOn() || bot->GetMapId() != 469 || !IsAreaOrCleaveAction(action))
        return 1.0f;
    Unit* target = AI_VALUE(Unit*, "current target");
    if (IsDominionCloneNear(bot, CLONE_RANGE) || (target && IsDominionCloneNear(target, CLONE_RANGE)))
        return 0.0f;
    return 1.0f;
}
