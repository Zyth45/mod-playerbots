/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AQ20ACTIONS_H
#define PLAYERBOTS_AQ20ACTIONS_H

#include "MovementActions.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"

class Aq20UseCrystalAction : public MovementAction
{
public:
    Aq20UseCrystalAction(PlayerbotAI* botAI, std::string const name = "aq20 use crystal") : MovementAction(botAI, name) {}
    bool Execute(Event event) override;
};
// CoA Combat (BU2): step out of Devastate's cone, behind or past 10 yd.
class Aq20BuruAvoidDevastateAction : public MovementAction
{
public:
    Aq20BuruAvoidDevastateAction(PlayerbotAI* botAI) : MovementAction(botAI, "aq20 buru avoid devastate") {}
    bool Execute(Event event) override;
};

// CoA Combat (OS1): back inside the closest active obelisk's shelter, on the bot's side of it.
class Aq20OssirianShelterAction : public MovementAction
{
public:
    Aq20OssirianShelterAction(PlayerbotAI* botAI) : MovementAction(botAI, "aq20 ossirian shelter") {}
    bool Execute(Event event) override;
};

#endif
