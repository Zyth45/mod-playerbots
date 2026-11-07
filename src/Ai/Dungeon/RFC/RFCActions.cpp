/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "RFCActions.h"
#include "AiObjectContext.h"
#include "Playerbots.h"

#include <cmath>

namespace
{
    // Whether a bot can stand at (x, y) near reference height z: ground found, no drop of more than
    // TARAGAMAN_ARM_MAX_DROP and no lava or slime over it (the pit around the pillars, where four bench runs
    // ended). The spot and the half-way point are both checked. z is set to the ground height.
    bool TaragamanSafeGround(Player* bot, float x, float y, float& z)
    {
        Map* map = bot->GetMap();
        float const refZ = bot->GetPositionZ();
        for (float part : { 0.5f, 1.0f })
        {
            float const px = bot->GetPositionX() + (x - bot->GetPositionX()) * part;
            float const py = bot->GetPositionY() + (y - bot->GetPositionY()) * part;
            float const ground = map->GetHeight(bot->GetPhaseMask(), px, py, refZ + 3.0f);
            if (ground <= INVALID_HEIGHT || std::fabs(ground - refZ) > TARAGAMAN_ARM_MAX_DROP)
                return false;
            LiquidData const liquid = map->GetLiquidData(bot->GetPhaseMask(), px, py, ground, bot->GetCollisionHeight(), {});
            if (liquid.Status != LIQUID_MAP_NO_WATER && liquid.Level > ground - 0.2f)
                return false;
            if (part == 1.0f)
                z = ground;
        }
        return true;
    }
}

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

// Once in cover the action steps aside: the bot keeps casting and healing from behind the pillar,
// and the multiplier only keeps movement from taking it back out.
bool TaragamanHideBehindPillarAction::isUseful()
{
    Unit* boss = TaragamanPillar::Boss(botAI, context);
    Creature* pillar = TaragamanPillar::Find(bot);
    return boss && pillar && !TaragamanPillar::InCover(bot, boss, pillar);
}

bool TaragamanAvoidEarthBreakAction::Execute(Event /*event*/)
{
    Creature* pillar = TaragamanPillar::EruptingNear(bot);
    if (!pillar)
        return false;

    if (bot->IsNonMeleeSpellCast(false))
        bot->InterruptNonMeleeSpells(false);

    // Forced, so a cover run that is still under way cannot hold the bot inside the zone. Straight away from the
    // pillar first, then 45 and 90 degrees either side: a bot between the pillar and the pit edge must not run
    // into the lava (Dungeon Clear review, 2.9.5). No safe spot: the bot stays rather than jumping in.
    float const base = pillar->GetAngle(bot);
    float const radius = EARTH_BREAK_RADIUS + EARTH_BREAK_SAFETY + 0.5f;
    for (float offset : { 0.0f, float(M_PI) / 4, -float(M_PI) / 4, float(M_PI) / 2, -float(M_PI) / 2 })
    {
        float x = pillar->GetPositionX() + std::cos(base + offset) * radius;
        float y = pillar->GetPositionY() + std::sin(base + offset) * radius;
        float z = bot->GetPositionZ();
        bot->GetMap()->CheckCollisionAndGetValidCoords(bot, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                                                       x, y, z);
        if (!TaragamanSafeGround(bot, x, y, z))
            continue;
        return MoveTo(bot->GetMapId(), x, y, z, false, false, false, true, MovementPriority::MOVEMENT_FORCED);
    }
    return false;
}

bool TaragamanHoldCenterAction::Execute(Event /*event*/)
{
    Unit* boss = TaragamanPillar::Boss(botAI, context);
    std::optional<Position> const spot = boss ? TaragamanArena::TankSpot(bot, boss) : std::nullopt;
    if (!spot || bot->GetExactDist2d(spot->GetPositionX(), spot->GetPositionY()) <= TARAGAMAN_TANK_TOLERANCE)
        return false;

    return MoveTo(bot->GetMapId(), spot->GetPositionX(), spot->GetPositionY(), spot->GetPositionZ(), false, false,
                  false, true, MovementPriority::MOVEMENT_COMBAT);
}

bool TaragamanHideBehindPillarAction::Execute(Event /*event*/)
{
    Unit* boss = TaragamanPillar::Boss(botAI, context);
    Creature* pillar = TaragamanPillar::Find(bot);
    if (!boss || !pillar || TaragamanPillar::InCover(bot, boss, pillar))
        return false;

    Position const spot = TaragamanPillar::CoverSpot(bot, boss, pillar);
    // Cover behind the pillar only on solid ground, never over the pit (Dungeon Clear review, 2.9.5).
    float coverZ = spot.GetPositionZ();
    if (!TaragamanSafeGround(bot, spot.GetPositionX(), spot.GetPositionY(), coverZ))
        return false;

    // Until the pillar has broken out, its Earth Break is not to be crossed; the bot waits where it is.
    if (TaragamanPillar::PathCrossesEruption(bot, pillar, spot))
    {
        if (bot->isMoving())
            bot->StopMoving();
        return true;
    }

    if (bot->IsNonMeleeSpellCast(false))
        bot->InterruptNonMeleeSpells(false);

    return MoveTo(bot->GetMapId(), spot.GetPositionX(), spot.GetPositionY(), coverZ, false, false,
                  false, true, MovementPriority::MOVEMENT_FORCED);
}
