/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
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
