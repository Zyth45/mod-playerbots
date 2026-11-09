/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AQ20TRIGGERS_H
#define PLAYERBOTS_AQ20TRIGGERS_H

#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "Trigger.h"

class Aq20MoveToCrystalTrigger : public Trigger
{
public:
    Aq20MoveToCrystalTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq20 move to crystal") {}
    bool IsActive() override;
};
// CoA Combat (BU2): Buru casts Devastate and the bot stands in its cone.
class Aq20BuruDevastateTrigger : public Trigger
{
public:
    Aq20BuruDevastateTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq20 buru devastate") {}
    bool IsActive() override;
};

// CoA Combat (OS1): Ossirian is up and the bot is out of every active obelisk's shelter.
class Aq20OssirianShelterTrigger : public Trigger
{
public:
    Aq20OssirianShelterTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq20 ossirian shelter") {}
    bool IsActive() override;
};

#endif
