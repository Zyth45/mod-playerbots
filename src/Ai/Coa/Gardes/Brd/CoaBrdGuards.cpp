/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "../CoaGardesCombat.h"

#include "../Common/CoaGardesGate.h"
#include "../Common/CoaGardesUnits.h"
#include "CoaIronHallDecision.h"
#include "CoaLyceumCloseUpDecision.h"

#include "Creature.h"
#include "Log.h"
#include "MoveSpline.h"
#include "Playerbots.h"
#include "Timer.h"

#include <unordered_map>

// Blackrock Depths, the per-bot halves of the Lyceum close-up and the Iron Hall safe spot, from Dungeon Clear
// (DcLyceumCloseUpAction.cpp, DcIronHallSafeSpotAction.cpp). The rules are the pure kernels next to this file; this
// feeds them a live bot and walks it to the answer. Rungs, not a world tick: what holds a bot in the wrong place
// (stock reach / MoveChase, follow, the heal reposition) is an action of the engines, so the walk has to outrank it.
namespace
{
    namespace B = CoaBrd;

    // The latches the trigger and the action share, per bot. thread_local: a bot is only evaluated on its own map's
    // thread, so no lock and no cross-map read; every latch also clears itself when the bot leaves the room or hall.
    struct ReissueSlot
    {
        uint32 ms = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f;

        // True when the same point was issued within the window (Dungeon Clear's ThrottledIssue).
        bool Throttled(float px, float py, float pz, float epsilon, uint32 windowMs)
        {
            if (ms && GetMSTimeDiffToNow(ms) < windowMs)
            {
                float const dx = x - px, dy = y - py, dz = z - pz;
                if (dx * dx + dy * dy + dz * dz <= epsilon * epsilon)
                    return true;
            }
            x = px;
            y = py;
            z = pz;
            ms = getMSTime() ? getMSTime() : 1u;
            return false;
        }
    };

    struct BrdState
    {
        uint32 lyceumCloseUpMs = 0;
        ReissueSlot lyceumIssue;
        uint32 ironHallHopMs = 0;
        uint32 ironHallStillMs = 0;
        float ironHallHopX = 0.0f;
        float ironHallHopY = 0.0f;
        ReissueSlot ironHallIssue;
    };

    BrdState& StateOf(Player* bot)
    {
        thread_local std::unordered_map<uint64, BrdState> states;
        return states[bot->GetGUID().GetRawValue()];
    }

    // ---- the Lyceum ----
    struct CloseUpView
    {
        CoaLyceumCloseUp::Verdict verdict{};
        bool fresh = false;
        float dist = 0.0f;
        float z = 0.0f;
    };

    bool InLyceum(WorldObject const* o)
    {
        return CoaLyceumCloseUp::InRoom(o->GetPositionX(), o->GetPositionY(), o->GetPositionZ());
    }

    CoaLyceumCloseUp::Gate LyceumGate(Player* bot, Player* tank)
    {
        CoaLyceumCloseUp::Gate g;
        g.hasTank = tank != nullptr;
        g.tankIsSelf = tank == bot;
        g.tankAlive = tank && tank->IsAlive();
        g.sameInstance = tank && tank->FindMap() == bot->FindMap();
        g.tankIsHuman = CoaGardesUnits::IsHuman(tank);
        g.botIsTank = PlayerbotAI::IsTank(bot);
        g.botInRoom = InLyceum(bot);
        g.tankInRoom = tank && InLyceum(tank);
        return g;
    }

    // Resolves the rung and STORES THE LATCH, so the trigger and the action agree.
    CloseUpView ProbeLyceum(Player* bot)
    {
        CloseUpView v;
        if (!bot || bot->isDead() || bot->GetMapId() != B::MAP_ID)
            return v;
        BrdState& st = StateOf(bot);
        Player* tank = CoaGardesUnits::Tank(bot);
        if (!bot->IsInCombat() || !CoaLyceumCloseUp::Applies(LyceumGate(bot, tank)))
        {
            st.lyceumCloseUpMs = 0;
            return v;
        }
        v.verdict = CoaLyceumCloseUp::Decide(bot->GetPositionX(), bot->GetPositionY(), tank->GetPositionX(),
                                             tank->GetPositionY(), st.lyceumCloseUpMs != 0);
        if (!v.verdict.close)
        {
            st.lyceumCloseUpMs = 0;
            return v;
        }
        v.fresh = st.lyceumCloseUpMs == 0;
        if (v.fresh)
            st.lyceumCloseUpMs = getMSTime() ? getMSTime() : 1u;
        v.dist = bot->GetExactDist2d(tank);
        v.z = tank->GetPositionZ();
        return v;
    }

    // ---- the Iron Hall ----
    constexpr float HOP_ARRIVE = 1.0f;       // a hop is done inside this of its slot
    constexpr uint32 HOP_MAX_MS = 8000;      // a hop not landed by now is re-decided
    constexpr uint32 STILL_GRACE_MS = 500;   // standing in the fire this long before a hop

    struct HopView
    {
        bool travel = false;
        bool fresh = false;
        CoaIronHall::Verdict verdict{};
        float x = 0.0f, y = 0.0f;
    };

    void ClearHop(BrdState& st)
    {
        st.ironHallHopMs = 0;
        st.ironHallStillMs = 0;
    }

    HopView ProbeIronHall(Player* bot)
    {
        HopView v;
        if (!bot || bot->isDead() || bot->GetMapId() != B::MAP_ID)
            return v;
        BrdState& st = StateOf(bot);
        if (!CoaIronHall::InHall(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()))
        {
            ClearHop(st);
            return v;
        }

        // A group with a reference tank only; a bot tank under a HUMAN reference tank is his off-tank, never moved.
        Player* tank = CoaGardesUnits::Tank(bot);
        if (!tank || !CoaGardesGate::MemberRungApplies(CoaGardesUnits::IsHuman(tank), PlayerbotAI::IsTank(bot)))
        {
            ClearHop(st);
            return v;
        }

        if (st.ironHallHopMs)
        {
            if (bot->GetExactDist2d(st.ironHallHopX, st.ironHallHopY) > HOP_ARRIVE &&
                GetMSTimeDiffToNow(st.ironHallHopMs) < HOP_MAX_MS)
            {
                v.travel = true;
                v.x = st.ironHallHopX;
                v.y = st.ironHallHopY;
                return v;
            }
            ClearHop(st);
        }

        CoaIronHall::Inputs in;
        in.x = bot->GetPositionX();
        in.y = bot->GetPositionY();
        in.moving = bot->isMoving();
        if (in.moving && bot->movespline && !bot->movespline->Finalized())
        {
            G3D::Vector3 const end = bot->movespline->FinalDestination();
            in.hasDest = true;
            in.destX = end.x;
            in.destY = end.y;
        }

        // The leader is the reference tank (in a Dungeon Clear run, its run leader): the way on is Magmus while he
        // stands, then the throne door.
        in.isLeader = tank == bot;
        if (in.isLeader)
        {
            if (Creature* magmus = bot->FindNearestCreature(B::NPC_MAGMUS, 250.0f, true))
            {
                in.goalX = magmus->GetPositionX();
                in.goalY = magmus->GetPositionY();
            }
            else
            {
                in.goalX = B::IRON_HALL_ZONES[B::IRON_HALL_ZONE_COUNT - 1].x;
                in.goalY = B::IRON_HALL_HOP_MIN_Y;
            }
        }
        else
        {
            in.goalX = tank->GetPositionX();
            in.goalY = tank->GetPositionY();
        }

        Unit* victim = bot->GetVictim();
        if (bot->IsInCombat() && victim && victim->IsAlive())
        {
            in.hasTarget = true;
            in.targetX = victim->GetPositionX();
            in.targetY = victim->GetPositionY();
            in.reach = PlayerbotAI::IsRanged(bot) ? sPlayerbotAIConfig.spellDistance - 2.0f
                                                  : bot->GetMeleeRange(victim) - 0.5f;
        }
        in.slotSeed = static_cast<uint32>(bot->GetGUID().GetCounter());

        CoaIronHall::Verdict const verdict = CoaIronHall::Decide(in);
        if (!verdict.hop)
        {
            st.ironHallStillMs = 0;
            return v;
        }
        if (!verdict.redirect)
        {
            if (!st.ironHallStillMs)
                st.ironHallStillMs = getMSTime() ? getMSTime() : 1u;
            if (GetMSTimeDiffToNow(st.ironHallStillMs) < STILL_GRACE_MS)
                return v;
        }

        st.ironHallStillMs = 0;
        st.ironHallHopX = verdict.spot.x;
        st.ironHallHopY = verdict.spot.y;
        st.ironHallHopMs = getMSTime() ? getMSTime() : 1u;
        v.travel = true;
        v.fresh = true;
        v.verdict = verdict;
        v.x = verdict.spot.x;
        v.y = verdict.spot.y;
        return v;
    }
}

bool CoaLyceumCloseUpTrigger::IsActive()
{
    // Map first: everywhere outside Blackrock Depths this costs one integer compare.
    if (!bot || bot->isDead() || bot->GetMapId() != B::MAP_ID)
        return false;
    if (!CoaGroupTank::ModuleGuardActs(bot, CoaGardesGate::LYCEUM_CLOSE_UP) || CoaGardesUnits::Controlled(bot))
        return false;
    return ProbeLyceum(bot).verdict.close;
}

bool CoaLyceumCloseUpAction::Execute(Event /*event*/)
{
    if (!bot)
        return false;
    CloseUpView const v = ProbeLyceum(bot);
    if (!v.verdict.close)
        return false;

    // The tank moves, so the aim moves: re-issue past 2yd of drift, and CLAIM the tick while the walk is in flight
    // (handing it back is how stock MoveChase turns the bot round halfway).
    BrdState& st = StateOf(bot);
    if (st.lyceumIssue.Throttled(v.verdict.x, v.verdict.y, v.z, 2.0f, 500) && bot->isMoving())
        return true;
    if (!MoveTo(bot->GetMapId(), v.verdict.x, v.verdict.y, v.z, false, false, false, false,
                MovementPriority::MOVEMENT_COMBAT))
    {
        st.lyceumCloseUpMs = 0;
        return false;
    }
    if (v.fresh)
        LOG_DEBUG("playerbots", "[coa gardes:{}] Lyceum: {:.1f}yd from the tank in a fight (leash {:.0f}yd), closing up "
                  "to ({:.1f}, {:.1f})", bot->GetName(), v.dist, CoaLyceumCloseUp::LEASH, v.verdict.x, v.verdict.y);
    return true;
}

bool CoaIronHallSafeSpotTrigger::IsActive()
{
    if (!bot || bot->isDead() || bot->GetMapId() != B::MAP_ID)
        return false;
    if (!CoaGroupTank::ModuleGuardActs(bot, CoaGardesGate::IRON_HALL) || CoaGardesUnits::Controlled(bot))
        return false;
    return ProbeIronHall(bot).travel;
}

bool CoaIronHallSafeSpotAction::Execute(Event /*event*/)
{
    if (!bot)
        return false;
    HopView const v = ProbeIronHall(bot);
    if (!v.travel)
        return false;

    BrdState& st = StateOf(bot);
    if (st.ironHallIssue.Throttled(v.x, v.y, B::IRON_HALL_FLOOR_Z, 1.0f, 1000) && bot->isMoving())
        return true;
    if (!MoveTo(bot->GetMapId(), v.x, v.y, B::IRON_HALL_FLOOR_Z, false, false, false, false,
                MovementPriority::MOVEMENT_COMBAT))
    {
        st.ironHallHopMs = 0;
        return false;
    }
    if (v.fresh)
        LOG_DEBUG("playerbots", "[coa gardes:{}] Iron Hall: {} the fire ({:.1f}yd from the nearest statue), hopping to "
                  "stop {} at ({:.1f}, {:.1f}){}", bot->GetName(), v.verdict.redirect ? "heading to a stop in" : "standing in",
                  CoaIronHall::FireDistance(bot->GetPositionX(), bot->GetPositionY()), v.verdict.zone, v.x, v.y,
                  v.verdict.inReach ? ", in reach of its target" : "");
    return true;
}

bool CoaIronHallHoldsFollow(Player* bot, Player* tank)
{
    if (!bot || !tank || bot == tank || bot->GetMapId() != B::MAP_ID || tank->FindMap() != bot->FindMap())
        return false;
    if (!CoaIronHall::InHall(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()))
        return false;
    if (StateOf(bot).ironHallHopMs)
        return true;
    return CoaIronHall::HoldFollow(bot->GetPositionX(), bot->GetPositionY(), tank->GetPositionX(), tank->GetPositionY());
}
