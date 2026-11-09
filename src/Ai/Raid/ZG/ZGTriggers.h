/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_ZGTRIGGERS_H
#define PLAYERBOTS_ZGTRIGGERS_H

#include "EncounterHelpers.h"
#include "RaidBossScripts.h"
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

// Venoxis in snake form: a bot that is not his victim stands in front of him, in reach of Venom Spit.
class VenoxisVenomSpitTrigger : public ZulGurubEncounterTrigger
{
public:
    VenoxisVenomSpitTrigger(PlayerbotAI* botAI) : ZulGurubEncounterTrigger(botAI, "venoxis venom spit") {}

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

// CoA Combat (lot 7): a kill order the session added (Thekal's zealots, Jin'do's totems), behind the option.
class ZulGurubCoaKillOrderTrigger : public RaidKillOrderMarkTrigger
{
public:
    ZulGurubCoaKillOrderTrigger(PlayerbotAI* botAI, std::string const name, std::string const bossName)
        : RaidKillOrderMarkTrigger(botAI, name, bossName)
    {
    }
    bool IsActive() override { return ZulGurubHelpers::ZgCoaFixesOn() && RaidKillOrderMarkTrigger::IsActive(); }
};

// CoA Combat (HK2): Blood Siphon is on and the bot carries no Poisonous Blood: it steps into a poison pool.
class HakkarSiphonPoolTrigger : public ZulGurubEncounterTrigger
{
public:
    HakkarSiphonPoolTrigger(PlayerbotAI* botAI) : ZulGurubEncounterTrigger(botAI, "hakkar siphon pool") {}

protected:
    bool IsActiveInEncounter() override;
};

// CoA Combat (EM1): the bot is Gri'lek's prey and he is close.
class GrilekPursuitTrigger : public ZulGurubEncounterTrigger
{
public:
    GrilekPursuitTrigger(PlayerbotAI* botAI) : ZulGurubEncounterTrigger(botAI, "grilek pursuit") {}

protected:
    bool IsActiveInEncounter() override;
};

#endif
