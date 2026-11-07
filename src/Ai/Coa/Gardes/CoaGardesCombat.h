/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_GARDES_COMBAT_H
#define COA_GARDES_COMBAT_H

#include "MovementActions.h"
#include "Multiplier.h"
#include "Position.h"
#include "Regroup/CoaRegroupDecision.h"
#include "Strategy.h"
#include "Trigger.h"

class Player;
class PlayerbotAI;

// The party combat guards that came from Dungeon Clear's member side (07/11, "DC is the autorun in the dungeon, the
// strategies are in playerbots"), active for every bot: in a Dungeon Clear run, behind a human Main Tank, in normal
// play. Each one asks CoaGroupTank::ModuleGuardActs first, so it stays silent for a run whose Dungeon Clear still does
// it itself. The tank they read is the group's reference tank (CoaGroupTank).
//   combat regroup   - Regroup/: a follower that cannot contribute from where it stands walks back to the fight;
//   escape leap ban  - EscapeLeap/: no Blink / Disengage in a dungeon or raid group;
//   room boss chase  - RoomBoss/: no chase that ends inside a sleeping room-aggro boss's wake sphere;
//   lyceum close up  - Brd/: Blackrock Depths' Lyceum, every member fights within 15yd of the tank;
//   iron hall        - Brd/: Blackrock Depths' Iron Hall, never stop in the statues' fire.

// --- combat regroup (combat engine) ---
class CoaCombatRegroupTrigger : public Trigger
{
public:
    CoaCombatRegroupTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa combat regroup", 1) {}
    bool IsActive() override;

private:
    // Per bot (one trigger object per bot); the action never reads it.
    CoaRegroupDecision::RegroupLatch latch;
};

class CoaCombatRegroupAction : public MovementAction
{
public:
    CoaCombatRegroupAction(PlayerbotAI* botAI) : MovementAction(botAI, "coa combat regroup") {}
    bool Execute(Event event) override;

private:
    bool FindStandoffPoint(Position const& center, float ringRadius, float maxRadius, float& x, float& y, float& z);
    Position lastDest;
    bool lastDestValid = false;
};

// --- Blackrock Depths: the Lyceum close-up (combat engine) and the Iron Hall safe spot (both engines) ---
class CoaLyceumCloseUpTrigger : public Trigger
{
public:
    CoaLyceumCloseUpTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa lyceum close up", 1) {}
    bool IsActive() override;
};

class CoaLyceumCloseUpAction : public MovementAction
{
public:
    CoaLyceumCloseUpAction(PlayerbotAI* botAI) : MovementAction(botAI, "coa lyceum close up") {}
    bool Execute(Event event) override;
};

class CoaIronHallSafeSpotTrigger : public Trigger
{
public:
    CoaIronHallSafeSpotTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa iron hall safe spot", 1) {}
    bool IsActive() override;
};

class CoaIronHallSafeSpotAction : public MovementAction
{
public:
    CoaIronHallSafeSpotAction(PlayerbotAI* botAI) : MovementAction(botAI, "coa iron hall safe spot") {}
    bool Execute(Event event) override;
};

// --- the bans: escape leaps, chases onto a sleeping room boss, chases out of the Lyceum pack ---
class CoaGardesCombatMultiplier : public Multiplier
{
public:
    CoaGardesCombatMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "coa gardes combat") {}
    float GetValue(Action* action) override;
};

class CoaGardesNonCombatMultiplier : public Multiplier
{
public:
    CoaGardesNonCombatMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "coa gardes noncombat") {}
    float GetValue(Action* action) override;
};

// "coa gardes combat" rides the combat engine, "coa gardes noncombat" the non-combat one; both for every bot.
class CoaGardesCombatStrategy : public Strategy
{
public:
    CoaGardesCombatStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}
    std::string const getName() override { return "coa gardes combat"; }
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    void InitMultipliers(std::vector<Multiplier*>& multipliers) override;
};

class CoaGardesNonCombatStrategy : public Strategy
{
public:
    CoaGardesNonCombatStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}
    std::string const getName() override { return "coa gardes noncombat"; }
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    void InitMultipliers(std::vector<Multiplier*>& multipliers) override;
};

#endif
