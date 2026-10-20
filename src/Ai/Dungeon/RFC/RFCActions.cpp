/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "RFCActions.h"
#include "AiObjectContext.h"
#include "Playerbots.h"

bool AttackOggleflintAddAction::isUseful() { return !botAI->IsHeal(bot); }
bool AttackOggleflintAddAction::Execute(Event /*event*/)
{
    Unit* add = RagefireEscort::Nearest(botAI, bot, context, "oggleflint",
                                        {NPC_RAGEFIRE_SHAMAN, NPC_RAGEFIRE_TROGG});
    if (!add || AI_VALUE(Unit*, "current target") == add)
        return false;

    return Attack(add);
}

bool AttackJergoshAddAction::isUseful() { return !botAI->IsHeal(bot); }
bool AttackJergoshAddAction::Execute(Event /*event*/)
{
    Unit* add = RagefireEscort::Nearest(botAI, bot, context, "jergosh the invoker",
                                        {NPC_SEARING_BLADE_CULTIST});
    if (!add || AI_VALUE(Unit*, "current target") == add)
        return false;

    return Attack(add);
}

bool AttackBazzalanAction::Execute(Event /*event*/)
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "bazzalan");
    if (!boss || AI_VALUE(Unit*, "current target") == boss)
        return false;

    return Attack(boss);
}

bool TaragamanAvoidFireNovaAction::Execute(Event /*event*/)
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "taragaman the hungerer");
    if (!boss)
        return false;

    float const distance = bot->GetExactDist2d(boss->GetPosition());
    float const wanted = FIRE_NOVA_RADIUS + FIRE_NOVA_SAFETY;
    if (distance >= wanted)
        return false;

    return MoveAway(boss, wanted - distance);
}

bool TaragamanHideBehindPillarAction::Execute(Event /*event*/)
{
    Unit* boss = TaragamanPillar::Boss(botAI, context);
    Creature* pillar = TaragamanPillar::Find(bot);
    if (!boss || !pillar)
        return false;

    // Holding still counts as success, so nothing of lower priority pulls the bot back into sight.
    if (TaragamanPillar::HasCore(pillar) &&
        TaragamanPillar::SafelyHidden(bot, boss, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()))
    {
        if (bot->isMoving())
            bot->StopMoving();
        return true;
    }

    Position const spot = TaragamanPillar::CoverSpot(bot, boss, pillar);
    if (bot->GetExactDist2d(spot.GetPositionX(), spot.GetPositionY()) <= EARTH_PILLAR_TOLERANCE)
        return true;

    if (bot->IsNonMeleeSpellCast(false))
        bot->InterruptNonMeleeSpells(false);

    return MoveTo(bot->GetMapId(), spot.GetPositionX(), spot.GetPositionY(), spot.GetPositionZ(), false, false,
                  false, true, MovementPriority::MOVEMENT_FORCED);
}
