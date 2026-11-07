/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_THREAT_LEAD_H
#define COA_THREAT_LEAD_H

class Player;

// The threat lead and the assist on the reference tank's fight, for every bot of a group: in a Dungeon Clear run, with
// a player tanking with the "tank main on" bar, and in ordinary play (combat guards that came from Dungeon Clear,
// 07/11: DcLeaderSignal::ShouldHoldDpsDamage and the "tank's fight" part of IsLeaderFightAssistWanted). The tank is
// CoaGroupTank::Reference; its fight clock is CoaTankFightClock. Never in a battleground or an arena.
namespace CoaThreatLead
{
    // The guard names for CoaGroupTank::ModuleGuardActs (Dungeon Clear declares the ones it still runs itself).
    constexpr char const* GuardThreatLead = "threat lead";
    constexpr char const* GuardTankFightAssist = "tank fight assist";

    // True while `bot`'s damage must wait for the tank's threat lead: AiPlayerbot.CoaThreatLeadDelay after the tank's
    // first hit (at most that plus 2.5 s from his combat flag without a hit). Never for a tank, a bot something
    // attacks, or a tank under AiPlayerbot.CoaThreatLeadPanicHp. Heals are never damage (the multiplier decides).
    bool ShouldHoldDpsDamage(Player* bot);

    // True when `bot` must go onto the tank's fight now: the tank (not `bot`) is fighting (a bot tank: it or a bot of
    // the group within AiPlayerbot.CoaTankFightSpread, latched AiPlayerbot.CoaTankFightLatch; a human tank: his own
    // combat), and the lead has run for a damage dealer. Never for a bot tank while a human tanks (his off-tanks take
    // his orders).
    bool IsTankFightAssistWanted(Player* bot);
}

#endif
