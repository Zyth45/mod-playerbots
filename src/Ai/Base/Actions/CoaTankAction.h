/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COATANKACTION_H
#define _PLAYERBOT_COATANKACTION_H

#include "ChatShortcutActions.h"

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

#endif
