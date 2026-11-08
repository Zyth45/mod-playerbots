/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_STCONTEXTS_H
#define PLAYERBOTS_STCONTEXTS_H

#include "NamedObjectContext.h"
#include "STActions.h"
#include "STTriggers.h"

class VanillaDungeonSTTriggerContext : public NamedObjectContext<Trigger>
{
public:
    VanillaDungeonSTTriggerContext()
    {
        creators["st hakkar suppressor"] = [](PlayerbotAI* ai) -> Trigger* { return new StHakkarSuppressorTrigger(ai); };
        creators["st hakkar flame"] = [](PlayerbotAI* ai) -> Trigger* { return new StHakkarFlameTrigger(ai); };
        creators["st hakkar blood"] = [](PlayerbotAI* ai) -> Trigger* { return new StHakkarBloodTrigger(ai); };
    }
};

class VanillaDungeonSTActionContext : public NamedObjectContext<Action>
{
public:
    VanillaDungeonSTActionContext()
    {
        creators["st hakkar suppressor"] = [](PlayerbotAI* ai) -> Action* { return new StHakkarSuppressorAction(ai); };
        creators["st hakkar flame"] = [](PlayerbotAI* ai) -> Action* { return new StHakkarFlameAction(ai); };
        creators["st hakkar blood"] = [](PlayerbotAI* ai) -> Action* { return new StHakkarBloodAction(ai); };
    }
};

#endif
