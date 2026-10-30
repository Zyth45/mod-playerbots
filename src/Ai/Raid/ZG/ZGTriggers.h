/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_ZGTRIGGERS_H
#define PLAYERBOTS_ZGTRIGGERS_H

#include "EncounterHelpers.h"
#include "Trigger.h"
#include "ZGHelpers.h"
#include <string>

class ZulGurubEncounterTrigger : public Trigger
{
public:
    ZulGurubEncounterTrigger(PlayerbotAI* botAI, std::string const name, int32 checkInterval = 1)
        : Trigger(botAI, name, checkInterval)
    {
    }

    bool IsActive() final
    {
        return EncounterHelpers::IsEncounterInProgress(bot, ZulGurubHelpers::ZG_MAP_ID) && IsActiveInEncounter();
    }

protected:
    virtual bool IsActiveInEncounter() = 0;
};

// Hakkar: a bot that is not his victim stands in front of him, in reach of Blood Scythe.
class HakkarBloodScytheTrigger : public ZulGurubEncounterTrigger
{
public:
    HakkarBloodScytheTrigger(PlayerbotAI* botAI) : ZulGurubEncounterTrigger(botAI, "hakkar blood scythe") {}

protected:
    bool IsActiveInEncounter() override;
};

// Hakkar: standing in a Pool of Corrupted Blood or of Poisoned Blood.
class HakkarBloodPoolTrigger : public ZulGurubEncounterTrigger
{
public:
    HakkarBloodPoolTrigger(PlayerbotAI* botAI) : ZulGurubEncounterTrigger(botAI, "hakkar blood pool") {}

protected:
    bool IsActiveInEncounter() override;
};

class BloodlordMandokirThreateningGazeTrigger : public ZulGurubEncounterTrigger
{
public:
    BloodlordMandokirThreateningGazeTrigger(PlayerbotAI* botAI)
        : ZulGurubEncounterTrigger(botAI, "bloodlord mandokir threatening gaze")
    {
    }

protected:
    bool IsActiveInEncounter() override;
};

#endif
