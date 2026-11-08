/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaOffTank.h"

#include "CoaGroupTank.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Unit.h"

bool CoaOffTank::IsOffTank(Player* bot)
{
    if (!sPlayerbotAIConfig.coaCombatOffTankEnable || !bot || !bot->GetGroup() || !PlayerbotAI::IsTank(bot))
        return false;
    Player* reference = CoaGroupTank::Reference(bot);
    return reference && reference != bot;
}

bool CoaOffTank::HeldByOtherTank(Player* bot, Unit* unit)
{
    Unit* victim = unit ? unit->GetVictim() : nullptr;
    Player* holder = victim ? victim->ToPlayer() : nullptr;
    return holder && holder != bot && holder->IsInSameRaidWith(bot) && PlayerbotAI::IsTank(holder);
}
