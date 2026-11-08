/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef COA_GARDES_BOT_STATE_H
#define COA_GARDES_BOT_STATE_H

#include "ObjectGuid.h"
#include "Timer.h"
#include "Value.h"

#include <vector>

// Per-bot state of the party combat guards, kept in the bot's own AI values (CoaGardesContext) rather than in
// thread_local maps: the MapUpdater workers change maps between updates, so a thread_local entry could be lost or
// read stale on the next tick.

// Blackrock Depths: the latches the Lyceum close-up and Iron Hall trigger and action share.
struct CoaBrdReissueSlot
{
    uint32 ms = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;

    // True when the same point was issued within the window (Dungeon Clear's ThrottledIssue).
    bool Throttled(float px, float py, float pz, float epsilon, uint32 windowMs)
    {
        if (ms && GetMSTimeDiffToNow(ms) < windowMs)
        {
            float const dx = x - px, dy = y - py, dz = z - pz;
            if (dx * dx + dy * dy + dz * dz <= epsilon * epsilon)
                return true;
        }
        x = px;
        y = py;
        z = pz;
        ms = getMSTime() ? getMSTime() : 1u;
        return false;
    }
};

struct CoaBrdState
{
    uint32 lyceumCloseUpMs = 0;
    CoaBrdReissueSlot lyceumIssue;
    uint32 ironHallHopMs = 0;
    uint32 ironHallStillMs = 0;
    float ironHallHopX = 0.0f;
    float ironHallHopY = 0.0f;
    CoaBrdReissueSlot ironHallIssue;
};

// "coa brd state"
class CoaBrdStateValue : public ManualSetValue<CoaBrdState&>
{
public:
    CoaBrdStateValue(PlayerbotAI* botAI) : ManualSetValue<CoaBrdState&>(botAI, data, "coa brd state") {}

    void Reset() override { data = CoaBrdState(); }

private:
    CoaBrdState data;
};

// The flagged room-aggro bosses near the bot, kept for a short while (the room boss chase ban).
struct CoaRoomBossMemo
{
    uint32 stampMs = 0;
    uint32 mapId = 0;
    uint32 instanceId = 0;
    std::vector<ObjectGuid> bosses;
};

// "coa room boss memo"
class CoaRoomBossMemoValue : public ManualSetValue<CoaRoomBossMemo&>
{
public:
    CoaRoomBossMemoValue(PlayerbotAI* botAI) : ManualSetValue<CoaRoomBossMemo&>(botAI, data, "coa room boss memo") {}

    void Reset() override { data = CoaRoomBossMemo(); }

private:
    CoaRoomBossMemo data;
};

#endif
