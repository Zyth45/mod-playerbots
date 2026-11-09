/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_ZGACTIONS_H
#define PLAYERBOTS_ZGACTIONS_H

#include "Action.h"
#include "MovementActions.h"

class BloodlordMandokirStopCausingThreatAction : public Action
{
public:
    BloodlordMandokirStopCausingThreatAction(PlayerbotAI* botAI)
        : Action(botAI, "bloodlord mandokir stop causing threat")
    {
    }

    bool Execute(Event event) override;
};

// Out of a boss's frontal cone to his nearer flank, at the same distance (melee stay in melee, ranged in range).
class ZulGurubMoveToBossFlankAction : public MovementAction
{
public:
    ZulGurubMoveToBossFlankAction(PlayerbotAI* botAI, std::string const name, std::string const bossName,
                                  float flankAngle)
        : MovementAction(botAI, name), bossName(bossName), flankAngle(flankAngle)
    {
    }
    bool Execute(Event event) override;

private:
    std::string const bossName;
    float const flankAngle;
};

// Hakkar: out of Blood Scythe's cone.
class HakkarAvoidBloodScytheAction : public ZulGurubMoveToBossFlankAction
{
public:
    HakkarAvoidBloodScytheAction(PlayerbotAI* botAI);
};

// Venoxis in snake form: out of Venom Spit's cone.
class VenoxisAvoidVenomSpitAction : public ZulGurubMoveToBossFlankAction
{
public:
    VenoxisAvoidVenomSpitAction(PlayerbotAI* botAI);
};

// CoA Combat (HK2): walk into the closest poison pool during Blood Siphon.
class HakkarMoveToSiphonPoolAction : public MovementAction
{
public:
    HakkarMoveToSiphonPoolAction(PlayerbotAI* botAI) : MovementAction(botAI, "hakkar move to siphon pool") {}
    bool Execute(Event event) override;
};

// CoA Combat (EM1): Gri'lek's prey keeps away from him.
class GrilekPreyRunAction : public MovementAction
{
public:
    GrilekPreyRunAction(PlayerbotAI* botAI) : MovementAction(botAI, "grilek prey run") {}
    bool Execute(Event event) override;
};

#endif
