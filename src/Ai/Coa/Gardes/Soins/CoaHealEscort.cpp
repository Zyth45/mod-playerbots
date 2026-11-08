/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "CoaHealEscort.h"

#include "Player.h"
#include "Unit.h"

namespace
{
    CoaHealEscort::Provider EscorteeOf;
}

void CoaHealEscort::SetProvider(Provider provider)
{
    EscorteeOf = std::move(provider);
}

Unit* CoaHealEscort::Escortee(Player const* member)
{
    if (!EscorteeOf || !member || !member->IsInWorld())
        return nullptr;
    Unit* escortee = EscorteeOf(member);
    // Never read a creature of another map (the provider answers for the run, not for the member's map).
    if (!escortee || !escortee->IsInWorld() || !escortee->IsAlive() || !escortee->FindMap() ||
        escortee->FindMap() != member->FindMap())
        return nullptr;
    return escortee;
}
