/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_GARDES_CONTEXT_H
#define COA_GARDES_CONTEXT_H

#include "NamedObjectContext.h"

class Action;
class Strategy;
class Trigger;
class UntypedValue;

// The values, triggers, actions and strategies of the combat guards that came from Dungeon Clear (07/11), for every
// bot. One registry for the whole migration (Builder and Dungeon Clear add their guards here, nowhere else).
class CoaGardesValueContext : public NamedObjectContext<UntypedValue>
{
public:
    CoaGardesValueContext();
};

class CoaGardesTriggerContext : public NamedObjectContext<Trigger>
{
public:
    CoaGardesTriggerContext();
};

class CoaGardesActionContext : public NamedObjectContext<Action>
{
public:
    CoaGardesActionContext();
};

class CoaGardesStrategyContext : public NamedObjectContext<Strategy>
{
public:
    CoaGardesStrategyContext();
};

#endif
