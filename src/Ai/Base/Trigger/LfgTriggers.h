/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_LFGTRIGGERS_H
#define PLAYERBOTS_LFGTRIGGERS_H

#include "Trigger.h"

class PlayerbotAI;

class LfgProposalActiveTrigger : public Trigger
{
public:
    LfgProposalActiveTrigger(PlayerbotAI* botAI) : Trigger(botAI, "lfg proposal active", 1000) {}

    bool IsActive() override;
};

// 3.2.1 (AiPlayerbot.CoaLfgRolePriority): a queued party of real players of its team misses the role this free random
// bot plays (CoaLfgRoleWanted): it queues now, not at its random roll.
class LfgRoleWantedTrigger : public Trigger
{
public:
    LfgRoleWantedTrigger(PlayerbotAI* botAI) : Trigger(botAI, "lfg role wanted", 5000) {}

    bool IsActive() override;
};

class UnknownDungeonTrigger : public Trigger
{
public:
    UnknownDungeonTrigger(PlayerbotAI* botAI) : Trigger(botAI, "unknown dungeon", 20 * 2000) {}

    bool IsActive() override;
};

#endif
