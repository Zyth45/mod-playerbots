/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COA_COMBAT_OWNED_GUARDS_H
#define PLAYERBOTS_COA_COMBAT_OWNED_GUARDS_H

// The party combat guards this module now owns for Dungeon Clear runs too. An autopilot built against this module
// (Dungeon Clear) reads these markers with __has_include / #if: when a guard's marker is 1, it drops its own copy,
// drops the guard's name from the list it hands to CoaGroupTank::SetHandledByDungeonClear and sets the run hooks that
// guard reads. An older module has no marker, and the autopilot keeps its copy.
//
// One line per guard, added in the commit that hands the guard over (CoA Combat, migration 3.0).
#define COA_COMBAT_OWNS_ESCAPE_LEAP_BAN 1  // A1: no hook
#define COA_COMBAT_OWNS_ROOM_BOSS_CHASE 1  // A2: CoaGardesRunHooks::SetRoomClearBoss
#define COA_COMBAT_OWNS_LYCEUM_CLOSE_UP 1  // A3: no hook
#define COA_COMBAT_OWNS_IRON_HALL 1  // A4: no hook; ordered under "brd ironhand gout" (62 > 57)
#define COA_COMBAT_OWNS_GROUND_HAZARDS 1  // A5: no hook; nested under the MC dodges
#define COA_COMBAT_OWNS_TARGET_EXCLUSION 1  // A6: CoaTargetExclusion::SetRunHooks
#define COA_COMBAT_OWNS_THREAT_LEAD 1  // A7: no hook; tank fight clock checked against DC's at the bench
#define COA_COMBAT_OWNS_TANK_FIGHT_ASSIST 1  // A8: no hook (camp branch stays in DC)
#define COA_COMBAT_OWNS_COMBAT_REGROUP 1  // A9: CoaGardesRunHooks::SetCampHoldPassive
#define COA_COMBAT_OWNS_HEAL_REPOSITION 1  // A10: CoaHealEscort::SetProvider (already wired)
#define COA_COMBAT_OWNS_TOP_OFF_PARTY 1  // A11: no hook
#define COA_COMBAT_OWNS_BOSS_STAND_DOWN 1  // A12: CoaBossStandDown::SetRosterBossProvider; DC reads CoaBossStandDown::IsActive

// Category B: guards Dungeon Clear ran alone, now in the module. Dungeon Clear calls or reads the module's instead.
#define COA_COMBAT_OWNS_BEAR_FORM_BEFORE_PULL 1  // B1: Dungeon Clear's pull calls CoaTankForm::EnsureBearForm
#define COA_COMBAT_OWNS_BREATH_GUARD 1  // B2: module world tick; Dungeon Clear's party gate calls CoaBreathGuard::AnyMemberUnderWater
#define COA_COMBAT_OWNS_STUCK_COMBAT_BREAKER 1  // B3: CoaStuckCombat::SetRunAllows (bot-tank rung, chess); DC keeps its run watchdog
#define COA_COMBAT_OWNS_COMBAT_PURGE 1  // B4: no hook; module world tick; A6's purgeBarred may read CoaCombatPurge::IsBarred

#endif
