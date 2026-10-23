/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_OTHERSIDETRIGGERS_H
#define PLAYERBOTS_OTHERSIDETRIGGERS_H

#include "Trigger.h"

class OtherSideInHazardTrigger : public Trigger
{
public:
    OtherSideInHazardTrigger(PlayerbotAI* botAI) : Trigger(botAI, "other side in hazard") {}
    bool IsActive() override;
};

class OtherSideEggToKickTrigger : public Trigger
{
public:
    OtherSideEggToKickTrigger(PlayerbotAI* botAI) : Trigger(botAI, "other side egg to kick") {}
    bool IsActive() override;
};

class OtherSideEggPhaseTrigger : public Trigger
{
public:
    OtherSideEggPhaseTrigger(PlayerbotAI* botAI) : Trigger(botAI, "other side egg phase") {}
    bool IsActive() override;
};

class OtherSideMuzahsTargetTrigger : public Trigger
{
public:
    OtherSideMuzahsTargetTrigger(PlayerbotAI* botAI) : Trigger(botAI, "other side muzah's target") {}
    bool IsActive() override;
};

class OtherSideSpiderTrigger : public Trigger
{
public:
    OtherSideSpiderTrigger(PlayerbotAI* botAI) : Trigger(botAI, "other side spider") {}
    bool IsActive() override;
};

#endif
