/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "Aq20Utils.h"
#include "SpellAuras.h"

uint32 const OSSIRIAN_BUFF = 25176;
uint32 const OSSIRIAN_DEBUFFS[] = {25177, 25178, 25180, 25181, 25183};
uint32 const OSSIRIAN_CRYSTAL_GO_ENTRY = 180619;

// Conquest of Azeroth (boss_ossirian_coa.cpp): Ossirian puts Sun Disc on himself on engage
// and removes it on reset. That fight never casts 25176, never applies 25177-25183 and
// has no crystals (GO 180619). Ossirian still carries 25176 from the stock
// creature_template_addon, so without this check the move-to-crystal trigger fires on CoA.
// No SpellDifficulty row, same id on all four difficulties.
uint32 const OSSIRIAN_COA_SUN_DISC = 2112709;

bool RaidAq20Utils::IsOssirianBuffActive(Unit* ossirian)
{
    return ossirian && ossirian->HasAura(OSSIRIAN_BUFF);
}

int32 RaidAq20Utils::GetOssirianDebuffTimeRemaining(Unit* ossirian)
{
    int32 retVal = 0xffffff;
    if (ossirian)
    {
        for (uint32 debuff : OSSIRIAN_DEBUFFS)
        {
            if (AuraApplication* auraApplication = ossirian->GetAuraApplication(debuff))
            {
                if (Aura* aura = auraApplication->GetBase())
                {
                    int32 duration = aura->GetDuration();
                    if (retVal > duration)
                        retVal = duration;
                }
            }
        }
    }
    return retVal;
}

GameObject* RaidAq20Utils::GetNearestCrystal(Unit* ossirian)
{
    return ossirian ? ossirian->FindNearestGameObject(OSSIRIAN_CRYSTAL_GO_ENTRY, 200.0f) : nullptr;
}

bool RaidAq20Utils::IsCoaOssirian(Unit* ossirian)
{
    return ossirian && ossirian->HasAura(OSSIRIAN_COA_SUN_DISC);
}
