/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "WCTriggers.h"

#include <cmath>
#include <mutex>
#include <unordered_map>

#include "AiObjectContext.h"
#include "Playerbots.h"
#include "Spell.h"
#include "Timer.h"

using namespace DungeonBoss;

namespace WailingCaverns
{
    namespace
    {
        struct OverrunRecord
        {
            OverrunLine line;
            uint32 until = 0;
        };

        std::mutex overrunLock;
        std::unordered_map<ObjectGuid, OverrunRecord> overruns;

        Spell* OverrunCast(Unit* skum)
        {
            for (uint32 type : { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL })
                if (Spell* spell = skum->GetCurrentSpell(type))
                    if (IsCastingNamed(skum, "Overrun"))
                        return spell;
            return nullptr;
        }
    }

    std::optional<OverrunLine> ActiveOverrun(Unit* skum)
    {
        if (!skum)
            return std::nullopt;

        uint32 const now = getMSTime();
        std::lock_guard<std::mutex> guard(overrunLock);
        if (skum->IsAlive())
            if (Spell* spell = OverrunCast(skum))
            {
                OverrunRecord& record = overruns[skum->GetGUID()];
                uint32 const until = now + uint32(std::max(0, spell->GetCastTimeRemaining())) + OVERRUN_CHARGE_MS;
                if (record.until <= now)
                    record.line = { skum->GetPositionX(), skum->GetPositionY(), skum->GetOrientation() };
                record.until = until;
                return record.line;
            }

        auto itr = overruns.find(skum->GetGUID());
        if (itr == overruns.end())
            return std::nullopt;
        if (!skum->IsAlive() || itr->second.until <= now)
        {
            overruns.erase(itr);
            return std::nullopt;
        }
        return itr->second.line;
    }

    float OverrunHalfWidth(Unit const* skum, Unit const* bot)
    {
        // The trample's range check counts both bodies.
        return OVERRUN_RADIUS + skum->GetBoundaryRadius() + bot->GetBoundaryRadius() + OVERRUN_MARGIN;
    }

    bool InOverrunPath(OverrunLine const& line, float halfWidth, float x, float y)
    {
        float const dx = x - line.x;
        float const dy = y - line.y;
        float const along = dx * std::cos(line.angle) + dy * std::sin(line.angle);
        float const aside = dy * std::cos(line.angle) - dx * std::sin(line.angle);
        return along > -halfWidth && along < OVERRUN_LENGTH + halfWidth && std::fabs(aside) < halfWidth;
    }
}

using namespace WailingCaverns;

bool WcSkumOverrunTrigger::IsActive()
{
    Unit* skum = AI_VALUE2(Unit*, "find target", "skum");
    std::optional<OverrunLine> const line = ActiveOverrun(skum);
    return line && InOverrunPath(*line, OverrunHalfWidth(skum, bot), bot->GetPositionX(), bot->GetPositionY());
}
