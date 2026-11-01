/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

/*
 * The damage a bot deals, scaled by its class or specialization (AiPlayerbot.CoaBotDamageByClass / BySpec): some
 * CoA rotations are far behind others, and a player may even them out. Only bots and their pets and summons: a
 * human player's damage is never touched. Against creatures only, unless AiPlayerbot.CoaBotDamageVsPlayers is on.
 * Registered before the group telemetry, which then measures the scaled damage.
 */

#include "AscensionSpecialization.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "ScriptMgr.h"
#include "Unit.h"

namespace
{
uint32 DamagePctOf(Unit* attacker, Unit* target)
{
    if (!attacker || !target ||
        (sPlayerbotAIConfig.coaBotDamagePctByClass.empty() && sPlayerbotAIConfig.coaBotDamagePctBySpec.empty()))
        return 100;
    Player* bot = attacker->GetCharmerOrOwnerPlayerOrPlayerItself();
    if (!bot || !GET_PLAYERBOT_AI(bot) || IsRealPlayer(bot))
        return 100;
    // Heals also pass through the periodic hook: only what hits an enemy is scaled.
    if (!attacker->IsHostileTo(target))
        return 100;
    if (!sPlayerbotAIConfig.coaBotDamageVsPlayers && target->GetCharmerOrOwnerPlayerOrPlayerItself())
        return 100;

    auto const spec = sPlayerbotAIConfig.coaBotDamagePctBySpec.find(GetAscensionActiveSpecialization(bot));
    if (spec != sPlayerbotAIConfig.coaBotDamagePctBySpec.end())
        return spec->second;
    auto const cls = sPlayerbotAIConfig.coaBotDamagePctByClass.find(bot->getClass());
    return cls != sPlayerbotAIConfig.coaBotDamagePctByClass.end() ? cls->second : 100;
}

template <typename T>
void Scale(T& damage, uint32 pct)
{
    if (pct != 100 && damage > 0)
        damage = T(uint64(damage) * pct / 100);
}
}  // namespace

class CoaBotDamageScaleUnitScript : public UnitScript
{
public:
    CoaBotDamageScaleUnitScript()
        : UnitScript("CoaBotDamageScaleUnitScript", true,
                     { UNITHOOK_MODIFY_MELEE_DAMAGE, UNITHOOK_MODIFY_SPELL_DAMAGE_TAKEN,
                       UNITHOOK_MODIFY_PERIODIC_DAMAGE_AURAS_TICK }) {}

    void ModifyMeleeDamage(Unit* target, Unit* attacker, uint32& damage) override
    {
        Scale(damage, DamagePctOf(attacker, target));
    }

    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellInfo const* /*spellInfo*/) override
    {
        Scale(damage, DamagePctOf(attacker, target));
    }

    void ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& damage,
                                       SpellInfo const* /*spellInfo*/) override
    {
        Scale(damage, DamagePctOf(attacker, target));
    }
};

void AddSC_coa_bot_damage_scale()
{
    new CoaBotDamageScaleUnitScript();
}
