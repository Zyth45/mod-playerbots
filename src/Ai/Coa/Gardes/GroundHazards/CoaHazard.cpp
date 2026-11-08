/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "CoaHazard.h"

#include "CoaHazardValues.h"
#include "DynamicObject.h"
#include "GameObject.h"
#include "ObjectAccessor.h"
#include "Playerbots.h"

namespace
{
    AiObjectContext* ContextOf(Player* bot)
    {
        if (!bot)
            return nullptr;
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        return botAI ? botAI->GetAiObjectContext() : nullptr;
    }
}

CoaHazard::LiveSet CoaHazard::Sample(Player* bot)
{
    LiveSet live;
    if (!bot)
        return live;

    // Each kind is gated on its own bool, so a map that registers only a pool or only a trap never computes the
    // values it has no rows for.
    uint32 const mapId = bot->GetMapId();
    bool const wantEmitters = CoaHazardRegistry::HasEmitters(mapId);
    bool const wantGround = CoaHazardRegistry::HasGroundHazards(mapId);
    bool const wantTraps = CoaHazardRegistry::HasTrapHazards(mapId);
    if (!wantEmitters && !wantGround && !wantTraps)
        return live;

    AiObjectContext* ctx = ContextOf(bot);
    if (!ctx)
        return live;

    // Registry order: creature emitters, then ground pools, then traps (NearestVacate keeps the first nearest).
    if (wantEmitters)
    {
        GuidVector const& hazards = ctx->GetValue<GuidVector>(CoaHazardKey::Hazards)->Get();
        live.reserve(live.size() + hazards.size());
        for (ObjectGuid guid : hazards)
        {
            Unit* u = ObjectAccessor::GetUnit(*bot, guid);
            if (!u)
                continue;

            // Re-check the registry on the resolved unit: the cached list can be 500ms stale across a map change.
            CoaHazardEmitter const* e = CoaHazardRegistry::Find(u->GetMapId(), u->GetEntry());
            if (!e || u->GetMapId() != mapId)
                continue;

            live.push_back(FromRow(*e, u->GetPositionX(), u->GetPositionY(), u->GetPositionZ(), u->IsAlive()));
        }
    }

    if (wantGround)
    {
        // A persistent area aura is not a unit: it resolves through GetDynamicObject, and resolving it IS the
        // liveness check (the pool exists until its duration expires).
        GuidVector const& pools = ctx->GetValue<GuidVector>(CoaHazardKey::GroundHazards)->Get();
        live.reserve(live.size() + pools.size());
        for (ObjectGuid guid : pools)
        {
            DynamicObject* d = ObjectAccessor::GetDynamicObject(*bot, guid);
            if (!d)
                continue;

            CoaGroundHazard const* g = CoaHazardRegistry::FindGround(d->GetMapId(), d->GetSpellId());
            if (!g || d->GetMapId() != mapId)
                continue;

            live.push_back(FromRow(*g, d->GetPositionX(), d->GetPositionY(), d->GetPositionZ(), true));
        }
    }

    if (wantTraps)
    {
        // A trap is neither a unit nor a dynamic object. Its loot state is not consulted: a trap cycles READY ->
        // ACTIVATED -> READY every couple of seconds, and that is not the difference between safe and unsafe ground.
        GuidVector const& traps = ctx->GetValue<GuidVector>(CoaHazardKey::TrapHazards)->Get();
        live.reserve(live.size() + traps.size());
        for (ObjectGuid guid : traps)
        {
            GameObject* go = ObjectAccessor::GetGameObject(*bot, guid);
            if (!go)
                continue;

            CoaTrapHazard const* t = CoaHazardRegistry::FindTrap(go->GetMapId(), go->GetEntry());
            if (!t || go->GetMapId() != mapId)
                continue;

            live.push_back(FromRow(*t, go->GetPositionX(), go->GetPositionY(), go->GetPositionZ(), true));
        }
    }

    return live;
}

bool CoaHazard::PointIsHot(Player* bot, float x, float y, float z)
{
    return bot && PointIsHot(Sample(bot), x, y, z);
}

bool CoaHazard::SegmentIsHot(Player* bot, float ax, float ay, float az, float bx, float by, float bz)
{
    return bot && SegmentIsHot(Sample(bot), ax, ay, az, bx, by, bz);
}

bool CoaHazard::LegIsHot(Player* bot, float x, float y, float z)
{
    return bot && SegmentIsHot(bot, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), x, y, z);
}

CoaHazard::VacateEmitter CoaHazard::NearestVacate(Player* bot)
{
    if (!bot)
        return VacateEmitter{};
    return NearestVacate(Sample(bot), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
}

bool CoaHazard::PointIsInVacateBand(Player* bot, float x, float y, float z)
{
    return bot && PointIsInVacateBand(Sample(bot), x, y, z);
}
