/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AQ40TRIGGERS_H
#define PLAYERBOTS_AQ40TRIGGERS_H

#include "Trigger.h"

class Aq40CthunLeaveStomachTrigger : public Trigger
{
public:
    Aq40CthunLeaveStomachTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 cthun leave stomach") {}
    bool IsActive() override;
};

class Aq40CthunDarkGlareTrigger : public Trigger
{
public:
    Aq40CthunDarkGlareTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 cthun dark glare") {}
    bool IsActive() override;
};

class Aq40CthunEldritchTrigger : public Trigger
{
public:
    Aq40CthunEldritchTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 cthun eldritch revelations") {}
    bool IsActive() override;
};

class Aq40CthunDevourTrigger : public Trigger
{
public:
    Aq40CthunDevourTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 cthun devour") {}
    bool IsActive() override;
};

class Aq40CthunCenterTrigger : public Trigger
{
public:
    Aq40CthunCenterTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 cthun center") {}
    bool IsActive() override;
};

class Aq40CthunPositionTrigger : public Trigger
{
public:
    Aq40CthunPositionTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 cthun position") {}
    bool IsActive() override;
};

class Aq40CthunTargetTrigger : public Trigger
{
public:
    Aq40CthunTargetTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 cthun target") {}
    bool IsActive() override;
};

#endif
