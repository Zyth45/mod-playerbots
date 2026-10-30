/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAMISSIONACTION_H
#define PLAYERBOTS_COAMISSIONACTION_H

#include "Action.h"
#include "NewRpgBaseAction.h"

class PlayerbotAI;
struct CoaMission;

// "mission gather [minutes] [mining|herbalism]", "mission status", "mission stop", whispered to an alt by its owner.
class CoaMissionAction : public Action
{
public:
    CoaMissionAction(PlayerbotAI* botAI) : Action(botAI, "mission") {}

    bool Execute(Event event) override;

    // requester null: a mission of the .playerbots missiontest bench, for a random bot.
    // orderItem: "mission order", gather that item until there are orderCount of it.
    // risk: "risk" was asked, a mission in High Risk is accepted.
    bool Start(Player* requester, uint32 minutes, uint32 skillId, std::string& error, uint32 orderItem = 0,
               uint32 orderCount = 0, bool risk = false);
};

// One step of the mission of the "mission" strategy: walk to the gathering spot, go from node to node, end it.
class CoaMissionUpdateAction : public NewRpgBaseAction
{
public:
    CoaMissionUpdateAction(PlayerbotAI* botAI) : NewRpgBaseAction(botAI, "mission update") {}

    bool Execute(Event event) override;
    bool isUseful() override;

private:
    bool DoTravel(CoaMission& mission, uint32 now);
    bool DoWork(CoaMission& mission, uint32 now);
    void TeleportToSpot(CoaMission const& mission);
};

#endif
