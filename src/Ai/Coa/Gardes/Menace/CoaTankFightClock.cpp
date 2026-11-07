/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaTankFightClock.h"

#include "CoaPlayerMainTank.h"
#include "CoaThreatLeadMath.h"
#include "Group.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "ScriptMgr.h"
#include "Timer.h"

#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace
{
    // Per tank, keyed by raw guid. A handful of entries (the tanks in combat on the realm, plus the tanks who fought
    // since their login). Hits are the hot path: they take the shared lock, and the exclusive one only for the one
    // hit per fight that stamps.
    std::shared_mutex Lock;
    std::unordered_map<uint64, CoaThreatLeadMath::FightClock> Clocks;

    bool GroupMainTank(Player const* player)
    {
        Group const* group = player->GetGroup();
        if (!group)
            return false;
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
            if (slot.guid == player->GetGUID())
                return slot.flags & MEMBER_FLAG_MAINTANK;
        return false;
    }
}

bool CoaTankFightClock::CanLead(Player const* player)
{
    if (!player || !player->GetGroup())
        return false;
    return PlayerbotAI::IsTank(const_cast<Player*>(player)) || CoaPlayerMainTank::IsSet(player) ||
           GroupMainTank(player);
}

void CoaTankFightClock::NoteEnterCombat(Player const* player)
{
    if (!CanLead(player))
        return;
    uint32 const now = getMSTime();
    std::unique_lock<std::shared_mutex> guard(Lock);
    Clocks[player->GetGUID().GetRawValue()].EnterCombat(now);
}

void CoaTankFightClock::NoteHit(Player const* player)
{
    if (!player || !player->IsInCombat())
        return;
    uint64 const key = player->GetGUID().GetRawValue();
    {
        std::shared_lock<std::shared_mutex> guard(Lock);
        auto it = Clocks.find(key);
        // No entry: not a tank, or a tank whose enter-combat edge came before the module saw him (the bar turned on
        // mid-fight). Nothing is held for that fight: Dungeon Clear's player-led rule, release rather than guess.
        if (it == Clocks.end())
            return;
        CoaThreatLeadMath::FightClock const& c = it->second;
        if (c.combatSinceMs != 0 && c.CurrentFirstHit() != 0)
            return;
    }
    uint32 const now = getMSTime();
    std::unique_lock<std::shared_mutex> guard(Lock);
    if (auto it = Clocks.find(key); it != Clocks.end())
        it->second.Hit(now);
}

void CoaTankFightClock::Forget(ObjectGuid guid)
{
    std::unique_lock<std::shared_mutex> guard(Lock);
    Clocks.erase(guid.GetRawValue());
}

void CoaTankFightClock::Stamps(Player const* tank, uint32& combatSinceMs, uint32& firstHitMs)
{
    combatSinceMs = 0;
    firstHitMs = 0;
    if (!tank)
        return;
    uint64 const key = tank->GetGUID().GetRawValue();
    bool const inCombat = tank->IsInCombat();
    {
        std::shared_lock<std::shared_mutex> guard(Lock);
        auto it = Clocks.find(key);
        if (it == Clocks.end())
            return;
        if (inCombat)
        {
            combatSinceMs = it->second.combatSinceMs;
            firstHitMs = it->second.CurrentFirstHit();
            return;
        }
        if (it->second.combatSinceMs == 0 && it->second.firstHitMs == 0)
            return;
    }
    // Out of combat: the fight is over.
    std::unique_lock<std::shared_mutex> guard(Lock);
    if (auto it = Clocks.find(key); it != Clocks.end())
        it->second.LeaveCombat();
}

bool CoaTankFightClock::EngagedLatched(Player const* tank, bool live, uint32 graceMs)
{
    if (!tank || graceMs == 0)
        return live;
    uint64 const key = tank->GetGUID().GetRawValue();
    uint32 const now = getMSTime();
    if (!live)
    {
        std::shared_lock<std::shared_mutex> guard(Lock);
        auto it = Clocks.find(key);
        if (it == Clocks.end() || it->second.engagedLatchMs == 0)
            return false;
    }
    std::unique_lock<std::shared_mutex> guard(Lock);
    return CoaThreatLeadMath::EngagedLatched(live, now, graceMs, Clocks[key].engagedLatchMs);
}

namespace
{
    // The tank's 0->1 combat edge: a new fight restarts the lead (Dungeon Clear's DungeonClearPullBrakeScript).
    class CoaGardesMenacePlayerScript : public PlayerScript
    {
    public:
        CoaGardesMenacePlayerScript()
            : PlayerScript("CoaGardesMenacePlayerScript", {PLAYERHOOK_ON_PLAYER_ENTER_COMBAT, PLAYERHOOK_ON_LOGOUT})
        {
        }

        void OnPlayerEnterCombat(Player* player, Unit* /*enemy*/) override { CoaTankFightClock::NoteEnterCombat(player); }

        void OnPlayerLogout(Player* player) override
        {
            if (player)
                CoaTankFightClock::Forget(player->GetGUID());
        }
    };

    // The tank's damage on a creature: the first one of a fight starts the lead (Dungeon Clear's
    // DungeonClearLeaderFirstHitScript).
    class CoaGardesMenaceUnitScript : public UnitScript
    {
    public:
        CoaGardesMenaceUnitScript() : UnitScript("CoaGardesMenaceUnitScript", true, {UNITHOOK_ON_DAMAGE}) {}

        void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
        {
            if (!damage || !attacker || !victim || victim->IsPlayer())
                return;
            if (Player* player = attacker->ToPlayer())
                CoaTankFightClock::NoteHit(player);
        }
    };
}

void AddSC_coa_gardes_menace()
{
    new CoaGardesMenacePlayerScript();
    new CoaGardesMenaceUnitScript();
}
