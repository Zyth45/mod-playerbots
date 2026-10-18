/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "SMTriggers.h"

#include "AiObjectContext.h"
#include "Playerbots.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellInfo.h"

#include <cstring>

namespace ScarletMonastery
{
    namespace
    {
        bool NameIs(SpellInfo const* info, char const* name)
        {
            return info && info->SpellName[LOCALE_enUS] && std::strcmp(info->SpellName[LOCALE_enUS], name) == 0;
        }
    }

    bool HasAuraNamed(Unit const* unit, char const* name)
    {
        for (auto const& [spellId, application] : unit->GetAppliedAuras())
            if (NameIs(application->GetBase()->GetSpellInfo(), name))
                return true;
        return false;
    }

    bool IsCastingNamed(Unit const* unit, char const* name)
    {
        for (uint32 type : { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL })
            if (Spell* spell = unit->GetCurrentSpell(type))
                if (NameIs(spell->GetSpellInfo(), name))
                    return true;
        return false;
    }

    bool IsDetonating(Unit const* doan)
    {
        return doan && doan->IsAlive() && (HasAuraNamed(doan, "Arcane Bubble") || IsCastingNamed(doan, "Detonation"));
    }

    bool IsWhirlwinding(Unit const* herod)
    {
        return herod && herod->IsAlive() && (HasAuraNamed(herod, "Whirlwind") || IsCastingNamed(herod, "Whirlwind"));
    }

    Unit* WhitemaneToFocus(PlayerbotAI* botAI, AiObjectContext* context)
    {
        Unit* whitemane = AI_VALUE2(Unit*, "find target", "high inquisitor whitemane");
        Unit* mograine = AI_VALUE2(Unit*, "find target", "scarlet commander mograine");
        if (!whitemane || !mograine || !whitemane->IsAlive() || !mograine->IsAlive())
            return nullptr;

        // While Mograine lies feigning death he cannot be selected; only once she has raised him are both up.
        if (!whitemane->isTargetableForAttack() || !mograine->isTargetableForAttack())
            return nullptr;

        return whitemane;
    }
}

using namespace ScarletMonastery;

bool SmDoanDetonationTrigger::IsActive()
{
    Unit* doan = AI_VALUE2(Unit*, "find target", "arcanist doan");
    return IsDetonating(doan) && bot->GetExactDist2d(doan) < DETONATION_SAFE_DISTANCE;
}

bool SmDoanRangedTrigger::IsActive()
{
    if (!botAI->IsRanged(bot))
        return false;

    Unit* doan = AI_VALUE2(Unit*, "find target", "arcanist doan");
    return doan && doan->IsAlive() && !IsDetonating(doan) && bot->GetExactDist2d(doan) < DOAN_RANGED_DISTANCE;
}

bool SmHerodWhirlwindTrigger::IsActive()
{
    if (botAI->IsTank(bot))
        return false;

    Unit* herod = AI_VALUE2(Unit*, "find target", "herod");
    return IsWhirlwinding(herod) && bot->GetExactDist2d(herod) < WHIRLWIND_SAFE_DISTANCE;
}

bool SmWhitemaneFocusTrigger::IsActive()
{
    if (botAI->IsTank(bot) || botAI->IsHeal(bot))
        return false;

    Unit* whitemane = WhitemaneToFocus(botAI, context);
    return whitemane && AI_VALUE(Unit*, "current target") != whitemane;
}
