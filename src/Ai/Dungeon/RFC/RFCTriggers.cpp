/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "RFCTriggers.h"
#include "AiObjectContext.h"
#include "Map.h"
#include "Playerbots.h"

#include <algorithm>
#include <cmath>
#include <list>
#include <limits>
#include <mutex>
#include <unordered_map>

namespace RagefireEscort
{
    Unit* Nearest(PlayerbotAI* botAI, Player* bot, AiObjectContext* context, std::string const& bossName,
                  std::initializer_list<uint32> entries)
    {
        Unit* boss = AI_VALUE2(Unit*, "find target", bossName);
        if (!boss)
            return nullptr;

        GuidVector targets = AI_VALUE(GuidVector, "possible targets no los");
        for (uint32 entry : entries)
        {
            for (auto& guid : targets)
            {
                Unit* unit = botAI->GetUnit(guid);
                if (!unit || unit->GetEntry() != entry || !unit->IsAlive() || !unit->isTargetableForAttack())
                    continue;

                if (unit->GetExactDist2d(boss->GetPosition()) <= RFC_ESCORT_RANGE)
                    return unit;
            }
        }

        return nullptr;
    }

    bool StillUp(PlayerbotAI* botAI, Player* bot, AiObjectContext* context, std::string const& bossName,
                 std::initializer_list<uint32> entries)
    {
        if (botAI->IsTank(bot))
            return false;

        return Nearest(botAI, bot, context, bossName, entries) != nullptr;
    }
}

namespace TaragamanPillar
{
    Unit* Boss(PlayerbotAI* botAI, AiObjectContext* context)
    {
        Unit* boss = AI_VALUE2(Unit*, "find target", "taragaman the hungerer");
        return boss && boss->IsAlive() ? boss : nullptr;
    }

    Creature* Find(Player* bot)
    {
        Creature* pillar = bot->FindNearestCreature(NPC_RUMBLING_EARTH, EARTH_PILLAR_SEARCH);
        return pillar && pillar->IsAlive() ? pillar : nullptr;
    }

    bool ShouldHide(PlayerbotAI* botAI, Player* bot, AiObjectContext* context)
    {
        Unit* boss = Boss(botAI, context);
        if (!boss || !Find(bot))
            return false;

        // The tank keeps Taragaman where he is until the nova is actually being cast; leaving
        // earlier would only drag him along to the pillar.
        if (botAI->IsTank(bot))
            return boss->HasUnitState(UNIT_STATE_CASTING) && boss->FindCurrentSpellBySpellId(SPELL_MASSIVE_FIRE_NOVA);

        return true;
    }

    bool HasCore(Creature* pillar)
    {
        return pillar->FindNearestGameObject(GO_EARTH_PILLAR_CORE, EARTH_PILLAR_CORE_SEARCH, true) != nullptr;
    }

    // Mirrors the area spell's own target check: from the boss's hit sphere to the bot's chest, with
    // M2 models ignored.
    bool Hidden(Player* bot, Unit* boss, float x, float y, float z)
    {
        float const chest = z + bot->GetCollisionHeight();
        float sx, sy, sz;
        boss->GetHitSpherePointFor({ x, y, chest }, sx, sy, sz);
        return !boss->GetMap()->isInLineOfSight(sx, sy, sz, x, y, chest, boss->GetPhaseMask(),
                                                LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::M2);
    }

    bool SafelyHidden(Player* bot, Unit* boss, float x, float y, float z)
    {
        if (!Hidden(bot, boss, x, y, z))
            return false;

        float const angle = boss->GetAngle(x, y) + float(M_PI) / 2.0f;
        float const sideX = std::cos(angle) * EARTH_PILLAR_SHADOW_MARGIN;
        float const sideY = std::sin(angle) * EARTH_PILLAR_SHADOW_MARGIN;
        return Hidden(bot, boss, x + sideX, y + sideY, z) && Hidden(bot, boss, x - sideX, y - sideY, z);
    }

    // Before the collision core has risen there is nothing to test the shadow against, so the bot
    // counts as covered once it stands on the spot behind the pillar.
    bool InCover(Player* bot, Unit* boss, Creature* pillar)
    {
        if (HasCore(pillar))
            return SafelyHidden(bot, boss, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());

        Position const spot = CoverSpot(bot, boss, pillar);
        return bot->GetExactDist2d(spot.GetPositionX(), spot.GetPositionY()) <= EARTH_PILLAR_TOLERANCE;
    }

    Position CoverSpot(Player* bot, Unit* boss, Creature* pillar)
    {
        float const away = boss->GetAngle(pillar);
        auto spotAt = [pillar, away](float offset, float radius)
        {
            float x = pillar->GetPositionX() + std::cos(away + offset) * radius;
            float y = pillar->GetPositionY() + std::sin(away + offset) * radius;
            float z = pillar->GetPositionZ();
            pillar->UpdateAllowedPositionZ(x, y, z);
            return Position(x, y, z, away + float(M_PI));
        };

        // Before the pillar has broken out, the spot waits outside its Earth Break.
        Position const fallback = spotAt(0.0f, IsErupting(pillar) ? EARTH_BREAK_RADIUS + EARTH_BREAK_SAFETY
                                                                   : EARTH_PILLAR_COVER);
        if (!HasCore(pillar))
            return fallback;

        // Directly behind the pillar first, then fanning out; the nearest safe spot wins so nobody
        // runs round the pillar when a closer one would do.
        static constexpr float Radii[] = { 2.0f, 2.5f, 3.0f, 4.0f, 5.0f };
        static constexpr float Offsets[] = { 0.0f, 0.26f, -0.26f, 0.52f, -0.52f, 0.79f, -0.79f };
        Position best = fallback;
        float bestDistance = std::numeric_limits<float>::max();
        for (float offset : Offsets)
            for (float radius : Radii)
            {
                Position const spot = spotAt(offset, radius);
                float const distance = bot->GetExactDist2d(spot);
                if (distance >= bestDistance ||
                    !SafelyHidden(bot, boss, spot.GetPositionX(), spot.GetPositionY(), spot.GetPositionZ()))
                    continue;

                best = spot;
                bestDistance = distance;
            }

        return best;
    }

    bool IsErupting(Creature* pillar)
    {
        return pillar->HasAura(SPELL_EARTH_PILLAR_WARNING);
    }

    Creature* EruptingNear(Player* bot)
    {
        std::list<Creature*> pillars;
        bot->GetCreatureListWithEntryInGrid(pillars, NPC_RUMBLING_EARTH, EARTH_BREAK_RADIUS + EARTH_BREAK_SAFETY);
        for (Creature* pillar : pillars)
            if (pillar->IsAlive() && IsErupting(pillar) &&
                bot->GetExactDist2d(pillar) < EARTH_BREAK_RADIUS + EARTH_BREAK_SAFETY)
                return pillar;

        return nullptr;
    }

    bool PathCrossesEruption(Player* bot, Creature* pillar, Position const& spot)
    {
        if (!IsErupting(pillar))
            return false;

        float const dx = spot.GetPositionX() - bot->GetPositionX();
        float const dy = spot.GetPositionY() - bot->GetPositionY();
        float const lengthSquared = dx * dx + dy * dy;
        float along = 0.0f;
        if (lengthSquared > 0.01f)
            along = std::clamp(((pillar->GetPositionX() - bot->GetPositionX()) * dx +
                                (pillar->GetPositionY() - bot->GetPositionY()) * dy) / lengthSquared, 0.0f, 1.0f);

        return pillar->GetExactDist2d(bot->GetPositionX() + dx * along, bot->GetPositionY() + dy * along) <
               EARTH_BREAK_RADIUS + EARTH_BREAK_SAFETY;
    }
}

namespace TaragamanArena
{
    namespace
    {
        std::mutex armLock;
        std::unordered_map<uint32, float> armAngles;

        // How far rock leads from the spawn point in this direction.
        float ArmLength(Map* map, uint32 phaseMask, Position const& center, float angle, float collisionHeight)
        {
            float reached = 0.0f;
            for (float distance = 1.0f; distance <= TARAGAMAN_ARM_MAX; distance += 1.0f)
            {
                float const x = center.GetPositionX() + std::cos(angle) * distance;
                float const y = center.GetPositionY() + std::sin(angle) * distance;
                float const ground = map->GetHeight(phaseMask, x, y, center.GetPositionZ() + 3.0f);
                if (ground <= INVALID_HEIGHT || std::fabs(ground - center.GetPositionZ()) > TARAGAMAN_ARM_MAX_DROP)
                    break;
                LiquidData const liquid = map->GetLiquidData(phaseMask, x, y, ground, collisionHeight, {});
                if (liquid.Status != LIQUID_MAP_NO_WATER && liquid.Level > ground - 0.2f)
                    break;
                reached = distance;
            }
            return reached;
        }
    }

    std::optional<Position> TankSpot(Player* bot, Unit* boss)
    {
        Creature* creature = boss ? boss->ToCreature() : nullptr;
        if (!creature)
            return std::nullopt;
        Position const center = creature->GetHomePosition();

        float angle = 0.0f;
        {
            std::lock_guard<std::mutex> guard(armLock);
            auto itr = armAngles.find(bot->GetInstanceId());
            if (itr != armAngles.end())
                angle = itr->second;
            else
            {
                float best = -1.0f;
                for (float degrees = 0.0f; degrees < 360.0f; degrees += TARAGAMAN_ARM_STEP_ANGLE)
                {
                    float const candidate = degrees * float(M_PI) / 180.0f;
                    float const length = ArmLength(bot->GetMap(), bot->GetPhaseMask(), center, candidate,
                                                   bot->GetCollisionHeight());
                    if (length > best)
                    {
                        best = length;
                        angle = candidate;
                    }
                }
                armAngles[bot->GetInstanceId()] = angle;
            }
        }

        float const offset = TARAGAMAN_TANK_OFFSET + boss->GetCombatReach() * 0.5f;
        float const x = center.GetPositionX() + std::cos(angle) * offset;
        float const y = center.GetPositionY() + std::sin(angle) * offset;
        float z = center.GetPositionZ();
        bot->UpdateAllowedPositionZ(x, y, z);
        return Position(x, y, z, Position::NormalizeOrientation(angle + float(M_PI)));
    }
}

bool OggleflintAddsTrigger::IsActive()
{
    // The Shaman is named first on purpose: it is the one that heals, and the action takes the
    // first entry it finds alive.
    return RagefireEscort::StillUp(botAI, bot, context, "oggleflint",
                                   {NPC_RAGEFIRE_SHAMAN, NPC_RAGEFIRE_TROGG});
}

bool JergoshAddsTrigger::IsActive()
{
    return RagefireEscort::StillUp(botAI, bot, context, "jergosh the invoker",
                                   {NPC_SEARING_BLADE_CULTIST});
}

bool BazzalanDpsTrigger::IsActive()
{
    // Bazzalan is the opposite case: he hits far harder than his Cultists, so he goes down first.
    Unit* boss = AI_VALUE2(Unit*, "find target", "bazzalan");
    if (!boss || !boss->isTargetableForAttack())
        return false;

    return !botAI->IsTank(bot);
}

bool TaragamanFireNovaTrigger::IsActive()
{
    if (!botAI->IsRanged(bot))
        return false;

    Unit* boss = AI_VALUE2(Unit*, "find target", "taragaman the hungerer");
    if (!boss || !boss->IsAlive())
        return false;

    if (TaragamanPillar::ShouldHide(botAI, bot, context))
        return false;

    return bot->GetExactDist2d(boss->GetPosition()) < FIRE_NOVA_RADIUS + FIRE_NOVA_SAFETY;
}

bool TaragamanEarthBreakTrigger::IsActive()
{
    return TaragamanPillar::Boss(botAI, context) && TaragamanPillar::EruptingNear(bot);
}

bool TaragamanTankSpotTrigger::IsActive()
{
    if (!botAI->IsMainTank(bot))
        return false;
    Unit* boss = TaragamanPillar::Boss(botAI, context);
    if (!boss || !boss->IsInCombat() || TaragamanPillar::ShouldHide(botAI, bot, context) ||
        TaragamanPillar::EruptingNear(bot))
        return false;
    std::optional<Position> const spot = TaragamanArena::TankSpot(bot, boss);
    return spot && bot->GetExactDist2d(spot->GetPositionX(), spot->GetPositionY()) > TARAGAMAN_TANK_TOLERANCE;
}

bool TaragamanMassiveFireNovaTrigger::IsActive()
{
    return TaragamanPillar::ShouldHide(botAI, bot, context);
}
