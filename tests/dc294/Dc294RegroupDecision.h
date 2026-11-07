// Dungeon Clear 2.9.4 (coa/2.9.4 @ 0590afb), Util/DcRegroupDecision.h + .cpp - copied verbatim as the non-regression ORACLE of the
// module's party combat guards. Only the includes are adapted so it compiles alone (no core header). Never edit.
/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCREGROUPDECISION_H
#define _PLAYERBOT_DCREGROUPDECISION_H

// Pure decision kernel for the contribution-gated combat regroup (Option B). Combat
// Regroup is no longer a distance tether: a follower only reconnects to the fight
// when it has NO useful work it can do from where it stands. This kernel is the
// role-aware "can this bot contribute?" test, extracted engine-free so it is
// unit-testable in isolation — mirroring DecidePull / DecideApproach / SelectHeal-
// Target. The trigger gathers the game-state reads into RegroupInputs, calls
// DecideCombatRegroup, then layers debounce/latch/cooldown on the verdict; the
// action turns a fire verdict into a role-correct standoff move. Nothing here
// touches a Player/Unit/context, so no game headers are needed.

namespace DcRegroupDecision
{
    // What the regroup rung should do this tick, before flap control is applied.
    //   None       — the bot can contribute (or must not move): stay put, let stock
    //                 combat / the rotation / the heal stack own the tick.
    //   Reconnect   — the bot cannot contribute from here: walk to a role-correct
    //                 standoff point with LOS on the fight (debounced).
    //   HardTether  — the bot has drifted past the outer tether: reconnect
    //                 regardless of the contribution test (bypasses debounce).
    enum class RegroupVerdict
    {
        None,
        Reconnect,
        HardTether
    };

    // Everything the verdict needs, resolved from live game state by the trigger.
    struct RegroupInputs
    {
        bool  isHealer = false;          // PlayerbotAI::IsHeal(bot)
        bool  isMelee = false;           // PlayerbotAI::IsMelee(bot) — role split for the action's radius
        bool  casting = false;           // a generic or channeled spell is in flight
        bool  ccd = false;               // stunned / fleeing / confused / rooted
        bool  hasVisibleAttacker = false;// stock LOS-filtered `attackers` value non-empty
        bool  hasLosTarget = false;      // live, valid, in-LOS `current target` (DC's own seed included)
        bool  hasHurtHealTarget = false; // DcKey::HealTarget resolves to a live hurt member (healers)
        bool  tankLos = false;           // bot->IsWithinLOSInMap(tank)
        float tankDist2d = 0.0f;         // bot->GetExactDist2d(tank)
        float healRange = 0.0f;          // botAI->GetRange("heal")
        float hardTether = 0.0f;         // DungeonClear.CombatRegroupDistance
        float slack = 0.0f;              // DungeonClear.CombatRegroupSlack
    };

    // The verdict, in priority order (see the plan §1):
    //   1. casting || ccd            -> None       (never clip a cast; never fight CC)
    //   2. tankDist2d > hardTether   -> HardTether (drifted-into-nowhere safety net)
    //   3. DPS (melee & ranged):
    //        hasVisibleAttacker      -> None       (rotation + reach/chase have work)
    //        hasLosTarget            -> None       (the rotation can shoot what it sees)
    //        else                    -> Reconnect
    //   4. Healer:
    //        hasHurtHealTarget       -> None       (HealReposition rel 41 owns that case)
    //        else (pre-positioning): !tankLos || tankDist2d > healRange - slack
    //                                -> Reconnect  else None
    inline RegroupVerdict DecideCombatRegroup(RegroupInputs const& in);
}

#endif  // _PLAYERBOT_DCREGROUPDECISION_H
/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */



namespace DcRegroupDecision
{
    inline RegroupVerdict DecideCombatRegroup(RegroupInputs const& in)
    {
        // Never clip a cast; never fight the CC handler. Same guard set as the
        // heal-reposition trigger. This wins even over the hard tether: a bot
        // mid-cast or stunned cannot move usefully anyway, so hold.
        if (in.casting || in.ccd)
            return RegroupVerdict::None;

        // Drifted past the outer tether: reconnect regardless of the contribution
        // test — the chased-runner-into-nowhere / left-far-behind case. Bypasses
        // debounce at the call site (the emergency path).
        if (in.tankDist2d > in.hardTether)
            return RegroupVerdict::HardTether;

        if (!in.isHealer)
        {
            // DPS (melee & ranged). A non-empty stock LOS-filtered attacker list
            // means the rotation + reach-spell(20)/MoveChase(30) have real work —
            // exactly the emptiness test ShouldAssistCampFight relies on. Empty =>
            // nothing visible to fight from here => reconnect. A ranged DPS whose
            // visible target is merely out of cast range is intentionally None:
            // stock `reach spell` closes toward the TARGET, which regroup-to-tank
            // used to fight (defect D1). A melee chasing a runner keeps a visible
            // attacker the whole chase => None until the hard tether.
            //
            // hasLosTarget is the second half of "can contribute", and it is the one
            // that keeps this rung from MUTING a working DPS. `attackers` only ever
            // contains mobs the bot or a groupmate holds real THREAT on (stock
            // AttackersValue walks GetThreatenedByMeList), so it reads empty in every
            // window where the party is combat-flagged but nobody has landed threat
            // yet — including the one DC manufactures itself, where the camp assist
            // seeds a target and SetInCombatWith()s the bot without any threat
            // relationship. In that window the bot HAS a live, valid, in-LOS target
            // its rotation could shoot, but a Reconnect verdict here latches and the
            // action (rel 29, above reach spell at 20 and the whole rotation at 5-20)
            // eats the tick — so the bot never casts, never gains threat, and the
            // empty-attackers state that armed the rung sustains itself. Standing
            // down whenever something shootable is in sight breaks that loop and
            // costs nothing: the around-the-corner case this rung exists for has no
            // in-LOS target by definition.
            return (in.hasVisibleAttacker || in.hasLosTarget) ? RegroupVerdict::None
                                                              : RegroupVerdict::Reconnect;
        }

        // Healer. HealReposition (rel 41) owns the hurt-target case — do not
        // double-own it. When a heal target is hurt, stand down here.
        if (in.hasHurtHealTarget)
            return RegroupVerdict::None;

        // Healer pre-positioning: nobody is hurt yet, but the healer is parked where
        // it could not heal the tank the moment damage starts (out of LOS, or beyond
        // heal range less a slack band so a step of tank movement doesn't drop it
        // straight back out). Reconnect to a heal-range LOS point; otherwise None.
        if (!in.tankLos || in.tankDist2d > (in.healRange - in.slack))
            return RegroupVerdict::Reconnect;

        return RegroupVerdict::None;
    }
}
