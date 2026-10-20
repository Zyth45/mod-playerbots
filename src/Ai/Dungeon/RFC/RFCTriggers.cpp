/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "RFCTriggers.h"
#include "AiObjectContext.h"
#include "Map.h"
#include "Playerbots.h"

#include <cmath>
#include <limits>

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

        Position const fallback = spotAt(0.0f, EARTH_PILLAR_COVER);
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

bool TaragamanMassiveFireNovaTrigger::IsActive()
{
    return TaragamanPillar::ShouldHide(botAI, bot, context);
}
