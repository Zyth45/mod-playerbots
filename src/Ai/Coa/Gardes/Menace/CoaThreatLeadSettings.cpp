/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaThreatLeadSettings.h"

#include "CoaGardesConfig.h"
#include "CoaThreatLeadMath.h"
#include "DBCStores.h"
#include "Map.h"
#include "Player.h"

#include <algorithm>
#include <string>

namespace
{
    float ReadFloat(std::string const& key, std::string const& alias, float fallback, float lo, float hi)
    {
        std::string const value = CoaConfigString(key, alias, std::to_string(fallback));
        float read = fallback;
        try
        {
            read = std::stof(value);
        }
        catch (...)
        {
            read = fallback;
        }
        return std::clamp(read, lo, hi);
    }

    // Dungeon Clear's rows (DcSettingsRegistry): default, clamp, heroic and raid layers.
    struct Loaded
    {
        float leadSec = 1.5f;
        float leadSecHeroic = 3.0f;
        float panicHp = 60.0f;
        float panicHpHeroic = 70.0f;
        float latchSec = 3.0f;
        float spread = 25.0f;
        float spreadRaid = 40.0f;

        Loaded()
        {
            leadSec = ReadFloat("AiPlayerbot.CoaThreatLeadDelay", "DungeonClear.PullPlayerReleaseDelay", 1.5f, 0.0f,
                                10.0f);
            leadSecHeroic = ReadFloat("AiPlayerbot.CoaThreatLeadDelayHeroic",
                                      "DungeonClear.PullPlayerReleaseDelay.Heroic", 3.0f, 0.0f, 10.0f);
            panicHp = ReadFloat("AiPlayerbot.CoaThreatLeadPanicHp", "DungeonClear.PullThreatLeadPanicHp", 60.0f, 0.0f,
                                100.0f);
            panicHpHeroic = ReadFloat("AiPlayerbot.CoaThreatLeadPanicHpHeroic",
                                      "DungeonClear.PullThreatLeadPanicHp.Heroic", 70.0f, 0.0f, 100.0f);
            latchSec = ReadFloat("AiPlayerbot.CoaTankFightLatch", "DungeonClear.PartyCombatLatch", 3.0f, 0.0f, 15.0f);
            spread = ReadFloat("AiPlayerbot.CoaTankFightSpread", "DungeonClear.PartyMaxSpread", 25.0f, 10.0f, 120.0f);
            spreadRaid = ReadFloat("AiPlayerbot.CoaTankFightSpreadRaid", "DungeonClear.PartyMaxSpread.Raid", 40.0f,
                                   10.0f, 120.0f);
        }
    };

    Loaded const& Get()
    {
        static Loaded const loaded;  // thread-safe first read
        return loaded;
    }

    // Dungeon Clear's heroic tier (DcDifficulty::IsHeroicTier): a heroic dungeon, or a heroic raid past Classic.
    bool HeroicTier(Map const* map)
    {
        if (!map || !map->IsHeroic())
            return false;
        if (!map->IsRaid())
            return true;
        MapEntry const* entry = map->GetEntry();
        return entry && entry->Expansion() > 0;
    }

    float Tier(Player const* member, float normal, float heroic, float raid, bool hasHeroic, bool hasRaid)
    {
        Map const* map = member ? member->FindMap() : nullptr;
        return CoaThreatLeadMath::Tiered(normal, heroic, raid, hasHeroic, hasRaid, HeroicTier(map),
                                         map && map->IsRaid());
    }
}

uint32 CoaThreatLeadSettings::LeadMs(Player const* member)
{
    Loaded const& s = Get();
    return uint32(Tier(member, s.leadSec, s.leadSecHeroic, 0.0f, true, false) * 1000.0f);
}

float CoaThreatLeadSettings::PanicHp(Player const* member)
{
    Loaded const& s = Get();
    return Tier(member, s.panicHp, s.panicHpHeroic, 0.0f, true, false);
}

uint32 CoaThreatLeadSettings::LatchMs(Player const* member)
{
    (void)member;
    return uint32(Get().latchSec * 1000.0f);
}

float CoaThreatLeadSettings::Spread(Player const* member)
{
    Loaded const& s = Get();
    return Tier(member, s.spread, 0.0f, s.spreadRaid, false, true);
}
