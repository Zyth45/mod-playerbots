/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaThreatLead.h"

#include "CoaGroupTank.h"
#include "CoaTankFightClock.h"
#include "CoaThreatLeadMath.h"
#include "CoaThreatLeadSettings.h"
#include "Group.h"
#include "Player.h"
#include "Playerbots.h"
#include "Timer.h"

namespace
{
    // The guards stand down in PvP, without a living bot, and where Dungeon Clear still runs the guard itself.
    bool GuardApplies(Player* bot, char const* guard)
    {
        if (!bot || !bot->IsAlive() || !bot->IsInWorld())
            return false;
        if (bot->InBattleground() || bot->InArena())
            return false;
        return CoaGroupTank::ModuleGuardActs(bot, guard);
    }

    bool IsHuman(Player const* player) { return !GET_PLAYERBOT_AI(const_cast<Player*>(player)); }

    // The tank's fight is live: the tank is in combat, or a BOT of the group close enough to share its fight is
    // (Dungeon Clear's AnyGroupMemberInCombat). A human other than the tank never counts: a human flagged in combat out
    // of the tank's sight would send the bots to a fight the tank is not in; nor a bot past the spread (a straggler a
    // far spawn tagged).
    bool TankFightLive(Player* tank)
    {
        if (tank->IsInCombat())
            return true;
        Group* group = tank->GetGroup();
        if (!group)
            return false;
        float const spread = CoaThreatLeadSettings::Spread(tank);
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member == tank || !member->IsAlive() || !member->IsInCombat())
                continue;
            if (member->FindMap() != tank->FindMap() || IsHuman(member))
                continue;
            if (tank->GetExactDist2d(member) <= spread)
                return true;
        }
        return false;
    }
}

bool CoaThreatLead::ShouldHoldDpsDamage(Player* bot)
{
    if (!GuardApplies(bot, GuardThreatLead))
        return false;
    Player* tank = CoaGroupTank::Reference(bot);
    if (!tank || tank == bot)
        return false;
    uint32 since = 0;
    uint32 firstHit = 0;
    CoaTankFightClock::Stamps(tank, since, firstHit);
    if (since == 0)
        return false;
    // Only a tank is exempt. A healer's damage spells wait too (a resto shaman's shock opened 0.0 s after the tank's
    // hit); its heals are never damaging actions.
    return CoaThreatLeadMath::ShouldHoldThreatLead(
        PlayerbotAI::IsTank(bot), !bot->getAttackers().empty(), since, firstHit, getMSTime(),
        CoaThreatLeadSettings::LeadMs(bot), CoaThreatLeadMath::NoHitCapMs, tank->GetHealthPct(),
        CoaThreatLeadSettings::PanicHp(bot));
}

bool CoaThreatLead::IsTankFightAssistWanted(Player* bot)
{
    if (!GuardApplies(bot, GuardTankFightAssist))
        return false;
    Player* tank = CoaGroupTank::Reference(bot);
    if (!tank || tank == bot)
        return false;
    bool const humanTank = IsHuman(tank);
    if (!CoaThreatLeadMath::MemberAssists(humanTank, PlayerbotAI::IsTank(bot)))
        return false;

    // Read the clock first on every call, so that a fight that ended is closed even when no assist is wanted.
    uint32 since = 0;
    uint32 firstHit = 0;
    CoaTankFightClock::Stamps(tank, since, firstHit);

    bool const wanted = humanTank ? tank->IsInCombat()
                                  : CoaTankFightClock::EngagedLatched(tank, TankFightLive(tank),
                                                                      CoaThreatLeadSettings::LatchMs(bot));
    if (!wanted)
        return false;

    // The threat lead, movement side: a damage dealer out of combat waits the lead from the tank's combat start
    // before walking into the fight. No stamp (the edge was missed): released at once.
    return CoaThreatLeadMath::ShouldReleaseFollower(PlayerbotAI::IsHeal(bot), bot->IsInCombat(), since, getMSTime(),
                                                    CoaThreatLeadSettings::LeadMs(bot), tank->GetHealthPct(),
                                                    CoaThreatLeadSettings::PanicHp(bot));
}
