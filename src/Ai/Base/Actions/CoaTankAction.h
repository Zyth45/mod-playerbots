/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COATANKACTION_H
#define _PLAYERBOT_COATANKACTION_H

#include "ChatShortcutActions.h"
#include "PositionAction.h"
#include "Trigger.h"

class Unit;

// "tank main on|off|status | target | taunt | swap [back] | hold | clear | status [all] | spec tank|dps", whispered
// to a bot of the player's group or sent by the SquidBots Lite Main Tank bar (SQBA). Answers TK and TS lines to the
// addon, a short sentence otherwise (contract of 04/11 with CoA Addon, docs ETUDE-MAIN-TANK-2.9.4).
class CoaTankAction : public PositionsResetAction
{
public:
    CoaTankAction(PlayerbotAI* botAI) : PositionsResetAction(botAI, "tank") {}

    bool Execute(Event event) override;

private:
    void Reply(std::string const& action, std::string const& result, uint8 icon = 0xFF, Unit* target = nullptr);
    void SendStatus(Player* tank);
    Unit* MasterTarget(Player* master);
    bool Taunt(Unit* target, std::string const& action);
    bool PinTarget(Unit* target, std::string const& action);
    void ReleaseHold();
};

// Placement in combat (RC11): a bot tank holding a point ("tank hold") and away from it walks back before anything
// else, a taunt on its given target thrown on the way; at the point, "stay" keeps it from reaching for its target.
class CoaPlacementAwayTrigger : public Trigger
{
public:
    CoaPlacementAwayTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa placement away", 1) {}
    bool IsActive() override;
};

class CoaPlacementMoveAction : public MoveToPositionAction
{
public:
    CoaPlacementMoveAction(PlayerbotAI* botAI) : MoveToPositionAction(botAI, "coa placement move", "stay") {}
    bool Execute(Event event) override;
    bool isUseful() override;
};

// Run by the bot's own update before its engine: the end of a given target (dead or gone) ends the order like
// "tank clear", said and told to the bar; a main tank flag left on a bot that is no longer a tank is handed over.
void CoaTankOrderTick(PlayerbotAI* botAI);

#endif
