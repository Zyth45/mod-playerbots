/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef PLAYERBOTS_STACTIONS_H
#define PLAYERBOTS_STACTIONS_H

#include "AttackAction.h"
#include "MovementActions.h"

class StHakkarSuppressorAction : public AttackAction
{
public:
    StHakkarSuppressorAction(PlayerbotAI* ai) : AttackAction(ai, "st hakkar suppressor") {}
    bool Execute(Event event) override;
};

class StHakkarFlameAction : public MovementAction
{
public:
    StHakkarFlameAction(PlayerbotAI* ai) : MovementAction(ai, "st hakkar flame") {}
    bool Execute(Event event) override;
};

class StHakkarBloodAction : public MovementAction
{
public:
    StHakkarBloodAction(PlayerbotAI* ai) : MovementAction(ai, "st hakkar blood") {}
    bool Execute(Event event) override;
};

#endif
