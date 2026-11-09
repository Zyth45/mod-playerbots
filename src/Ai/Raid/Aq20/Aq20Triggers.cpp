/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "Aq20Triggers.h"
#include "Aq20Utils.h"
#include "Playerbots.h"

bool Aq20MoveToCrystalTrigger::IsActive()
{
    if (Unit* boss = AI_VALUE2(Unit*, "find target", "ossirian the unscarred"))
    {
        // The Conquest of Azeroth fight has no crystals to run to
        if (boss->IsInCombat() && !RaidAq20Utils::IsCoaOssirian(boss))
        {
            // if buff is active move to crystal
            if (RaidAq20Utils::IsOssirianBuffActive(boss))
                return true;

            // if buff is not active a debuff will be, buff becomes active once debuff expires
            // so move to crystal when debuff almost done, or based debuff time left and
            // distance bot is from crystal (ie: start moving early enough to make it)
            int32 debuffTimeRemaining = RaidAq20Utils::GetOssirianDebuffTimeRemaining(boss);
            if (debuffTimeRemaining < 5000)
                return true;
            if (debuffTimeRemaining < 30000)
            {
                if (GameObject* crystal = RaidAq20Utils::GetNearestCrystal(boss))
                {
                    float botDist = bot->GetDistance(crystal);
                    float timeToReach = botDist / bot->GetSpeed(MOVE_RUN);
                    // bot should ideally activate crystal a ~5 seconds early (due to time it takes for crystal
                    // to activate) so aim to get there in time to do so
                    return debuffTimeRemaining - 5000 < timeToReach * 1000.0f;
                }
            }
        }
    }
    return false;
}

// ---- CoA Combat (lot 7, docs/AQ-ETAT-3.0.md) ----

bool Aq20BuruDevastateTrigger::IsActive()
{
    if (!AqCoaFixesOn())
        return false;
    Unit* buru = bot->FindNearestCreature(CoaAq20::NPC_BURU, 40.0f);
    if (!buru || !buru->IsAlive() || !buru->FindCurrentSpellBySpellId(CoaAq20::SPELL_DEVASTATE))
        return false;
    return bot->GetDistance2d(buru) < CoaAq20::DEVASTATE_YARDS + 1.0f && buru->HasInArc(float(M_PI) / 2.0f + 0.3f, bot);
}

bool Aq20OssirianShelterTrigger::IsActive()
{
    if (!AqCoaFixesOn() || !bot->IsInCombat())
        return false;
    Unit* ossirian = AI_VALUE2(Unit*, "find target", "ossirian the unscarred");
    if (!ossirian || !ossirian->IsAlive() || !RaidAq20Utils::IsCoaOssirian(ossirian))
        return false;
    Unit* obelisk = RaidAq20Utils::GetNearestActiveObelisk(bot);
    return obelisk && bot->GetExactDist2d(obelisk) > CoaAq20::OBELISK_KEEP_YARDS + 2.0f;
}
