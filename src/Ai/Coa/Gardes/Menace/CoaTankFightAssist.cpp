/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaTankFightAssist.h"

#include "CoaGroupTank.h"
#include "CoaThreatLead.h"
#include "CoaThreatLeadMath.h"
#include "CoaThreatLeadSettings.h"
#include "Event.h"
#include "Group.h"
#include "PathGenerator.h"
#include "Playerbots.h"
#include "ServerFacade.h"
#include "Timer.h"

#include <cmath>
#include <iterator>
#include <unordered_set>
#include <vector>

namespace
{
    // Dungeon Clear's assist tuning (DcFollowerActions.cpp, DcActionShared.h).
    constexpr uint32 RefusedGiveUpMs = 5 * 1000;
    constexpr uint32 GivenUpMs = 60 * 1000;
    constexpr float CombatApproachRange = 10.0f;

    bool IsHuman(Player const* player) { return !GET_PLAYERBOT_AI(const_cast<Player*>(player)); }

    // Same level, or a complete route that ends on the mob's level without a long detour (Dungeon Clear's
    // IsLevelReachable). Only a candidate that would win the rank pays for the probe.
    bool LevelReachable(Player* bot, Unit* u)
    {
        if (std::fabs(u->GetPositionZ() - bot->GetPositionZ()) <= CoaThreatLeadMath::ZLevelTolerance)
            return true;
        PathGenerator gen(bot);
        gen.CalculatePath(u->GetPositionX(), u->GetPositionY(), u->GetPositionZ(), false);
        if (gen.GetPathType() != PATHFIND_NORMAL)
            return false;
        Movement::PointsArray const& path = gen.GetPath();
        if (path.empty() || std::fabs(path.back().z - u->GetPositionZ()) > CoaThreatLeadMath::ZLevelTolerance)
            return false;
        return CoaThreatLeadMath::WithinDetour(bot->GetDistance(u), gen.getPathLength());
    }

    // Every unit holding `member` in combat: its threat references (a mob that tagged from range and stands off is in
    // no attacker list), its attackers and its victim (Dungeon Clear's CollectCombatHolders).
    void CollectCombatHolders(Unit* member, std::vector<Unit*>& out)
    {
        std::unordered_set<uint64> seen;
        for (Unit* u : out)
            if (u)
                seen.insert(u->GetGUID().GetRawValue());
        auto const collect = [&](Unit* u)
        {
            if (u && seen.insert(u->GetGUID().GetRawValue()).second)
                out.push_back(u);
        };
        for (auto const& kv : member->GetCombatManager().GetPvECombatRefs())
            if (CombatReference* ref = kv.second)
                collect(ref->GetOther(member));
        for (Unit* attacker : member->getAttackers())
            collect(attacker);
        collect(member->GetVictim());
    }

    bool Usable(Player* bot, Unit* u)
    {
        return u && u->IsAlive() && u->IsInWorld() && u->FindMap() == bot->FindMap() && bot->IsValidAttackTarget(u);
    }
}

bool CoaTankFightAssistState::GivenUp(ObjectGuid guid, uint32 now) const
{
    auto it = givenUpUntilMs.find(guid.GetRawValue());
    return it != givenUpUntilMs.end() && now < it->second;
}

void CoaTankFightAssistState::GiveUp(ObjectGuid guid, uint32 now, uint32 forMs)
{
    // Drop the lapsed ones on the way: the list stays a few entries long.
    for (auto it = givenUpUntilMs.begin(); it != givenUpUntilMs.end();)
        it = now >= it->second ? givenUpUntilMs.erase(it) : std::next(it);
    givenUpUntilMs[guid.GetRawValue()] = now + forMs;
    refusedTarget = ObjectGuid::Empty;
    refusedSinceMs = 0;
}

bool CoaTankFightAssistTrigger::IsActive()
{
    // Healers place themselves on their heal target (heal reposition), never on the pack.
    if (!bot || !bot->IsAlive() || PlayerbotAI::IsHeal(bot))
        return false;
    return CoaThreatLead::IsTankFightAssistWanted(bot);
}

bool CoaTankFightAssistCombatTrigger::IsActive()
{
    if (!bot || !bot->IsAlive() || !bot->IsInCombat() || PlayerbotAI::IsHeal(bot))
        return false;
    if (!CoaThreatLead::IsTankFightAssistWanted(bot))
        return false;
    // Stand down when a mob is in reach AND in sight: the rotation fights it. The reach test is the action's own, so
    // the trigger and the action never disagree about the same mob (Dungeon Clear issue #18: casters pinned at 30 yd).
    GuidVector const& attackers = AI_VALUE(GuidVector, "attackers");
    if (attackers.empty())
        return true;
    bool const isMelee = botAI->IsMelee(bot);
    float const meleeRange = bot->GetCombatReach() + 5.0f;
    float const spellRange = botAI->GetRange("spell");
    for (ObjectGuid const& guid : attackers)
    {
        Unit* u = ObjectAccessor::GetUnit(*bot, guid);
        if (!u || !u->IsAlive())
            continue;
        if (CoaThreatLeadMath::IsWithinAssistAttackRange(isMelee, bot->GetExactDist(u), meleeRange, spellRange,
                                                         bot->GetCombatReach() + u->GetCombatReach()) &&
            bot->IsWithinLOSInMap(u))
            return false;
    }
    return true;
}

Unit* CoaTankFightAssistAction::PickFightTarget(Player* tank, CoaTankFightAssistState const& state, uint32 now)
{
    Unit* best = nullptr;
    float bestDist = 0.0f;
    auto consider = [&](Unit* u)
    {
        if (!Usable(bot, u))
            return;
        float const d = bot->GetExactDist2d(u);
        if (best && d >= bestDist)
            return;
        if (state.GivenUp(u->GetGUID(), now) || !LevelReachable(bot, u))
            return;
        best = u;
        bestDist = d;
    };
    auto considerFrom = [&](Unit* src)
    {
        std::vector<Unit*> holders;
        CollectCombatHolders(src, holders);
        for (Unit* h : holders)
            consider(h);
    };

    // 1. The tank's own fight (the pack it holds).
    considerFrom(tank);
    if (best)
        return best;

    // 2. Else what a groupmate near the tank is fighting: the tank's fight, not a far straggler's.
    if (Group* group = bot->GetGroup())
    {
        float const reach = CoaThreatLeadSettings::Spread(bot) * 1.5f;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* m = ref->GetSource();
            if (!m || m == bot || !m->IsAlive() || !m->IsInCombat() || m->FindMap() != bot->FindMap())
                continue;
            if (m->GetExactDist2d(tank) > reach)
                continue;
            considerFrom(m);
        }
    }
    return best;
}

bool CoaTankFightAssistAction::Execute(Event /*event*/)
{
    Player* tank = CoaGroupTank::Reference(bot);
    if (!tank || tank == bot)
        return false;
    bool const humanTank = IsHuman(tank);
    if (!CoaThreatLeadMath::MemberAssists(humanTank, PlayerbotAI::IsTank(bot)))
        return false;

    CoaTankFightAssistState& state = AI_VALUE(CoaTankFightAssistState&, "coa tank fight assist state");
    uint32 const now = getMSTime();
    float const fightRadius = CoaThreatLeadSettings::Spread(bot) * 1.5f;

    // A usable target the bot already has wins (an instance strategy's kill order seeds one; overwriting it every
    // tick ping-pongs the target and the rotation runs on neither), within the tank's fight and reachable.
    Unit* target = nullptr;
    if (Unit* cur = AI_VALUE(Unit*, "current target"))
        if (Usable(bot, cur) && cur->GetExactDist2d(tank) <= fightRadius && !state.GivenUp(cur->GetGUID(), now) &&
            LevelReachable(bot, cur))
            target = cur;

    if (!target)
    {
        // A human tank: "the tank hits X, the damage dealers come onto X".
        if (humanTank)
            if (Unit* victim = tank->GetVictim(); Usable(bot, victim) && !state.GivenUp(victim->GetGUID(), now) &&
                                                  LevelReachable(bot, victim))
                target = victim;
        if (!target)
            target = PickFightTarget(tank, state, now);
        // Nothing concrete: stand down (never walk onto the tank's back); the pick resolves within a tick or two
        // once the tank's attackers fill in.
        if (!target)
            return false;
        // Seed it, so that both engines have a current target: the mob is on the tank, never on this bot's own
        // attacker list, and stock target selection finds nothing while it is out of sight.
        bot->SetSelection(target->GetGUID());
        context->GetValue<Unit*>("current target")->Set(target);
    }
    if (!bot->IsInCombat())
        bot->SetInCombatWith(target);

    // Non-combat engine (a bot flagged in combat with nothing of its own stays there): switch to the combat engine at
    // once; the combat side below drives the approach, and the multiplier keeps "drop target" from bouncing it back
    // while the mob is out of sight.
    if (!combatEngine)
    {
        botAI->ChangeEngine(BOT_STATE_COMBAT);
        return true;
    }

    float const dist = bot->GetExactDist(target);
    bool const isMelee = botAI->IsMelee(bot);
    float const meleeRange = bot->GetCombatReach() + target->GetCombatReach() + 1.0f;
    float const range = isMelee ? meleeRange : (botAI->GetRange("spell") - CONTACT_DISTANCE);
    bool const inAttackRange = CoaThreatLeadMath::IsWithinAssistAttackRange(
        isMelee, dist, meleeRange, botAI->GetRange("spell"), bot->GetCombatReach() + target->GetCombatReach());

    // In sight: the stock reach actions place the bot at its class's distance. In range, engage (face and attack, so
    // the rotation fires) and yield so the rotation runs this tick; out of range, just yield.
    if (bot->IsWithinLOSInMap(target))
    {
        state.refusedSinceMs = 0;
        if (inAttackRange)
        {
            if (!bot->HasInArc(CAST_ANGLE_IN_FRONT, target))
                ServerFacade::instance().SetFacingTo(bot, target);
            bot->Attack(target, isMelee);
        }
        return false;
    }

    // Out of sight: close on the MOB (never the tank) to round the corner. Combat priority only for the last stretch,
    // so a long run stops and fights what it pulls on the way.
    MovementPriority const priority = dist <= range + CombatApproachRange ? MovementPriority::MOVEMENT_COMBAT
                                                                           : MovementPriority::MOVEMENT_NORMAL;
    bool const closing = MoveTo(target->GetMapId(), target->GetPositionX(), target->GetPositionY(),
                                target->GetPositionZ(), false, false, false, false, priority);

    // Every route refused for long enough: give the mob up for a while and drop it as the target.
    bool const sameTarget = state.refusedTarget == target->GetGUID();
    state.refusedTarget = target->GetGUID();
    if (CoaThreatLeadMath::AssistCloseGivenUp(closing || bot->isMoving(), sameTarget, now, RefusedGiveUpMs,
                                              state.refusedSinceMs))
    {
        state.GiveUp(target->GetGUID(), now, GivenUpMs);
        bot->SetSelection(ObjectGuid::Empty);
        context->GetValue<Unit*>("current target")->Set(nullptr);
        return false;
    }
    return true;
}
