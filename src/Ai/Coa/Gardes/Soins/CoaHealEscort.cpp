/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
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
