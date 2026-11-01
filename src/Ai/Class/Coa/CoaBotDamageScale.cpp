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
#include <atomic>
#include <memory>
#include <unordered_map>

void LoadCoaBotDamageConfig(PlayerbotAIConfig& config);

namespace
{
// What the map threads read: a copy of the settings, swapped whole by ".reload config" while they run.
struct DamageTables
{
    std::unordered_map<uint32, uint32> byClass;
    std::unordered_map<uint32, uint32> bySpec;
    bool vsPlayers = false;
};

std::shared_ptr<DamageTables const> Tables;
std::atomic<bool> Loaded{false};
std::atomic<bool> Active{false};

void PublishTables()
{
    auto tables = std::make_shared<DamageTables>();
    tables->byClass = sPlayerbotAIConfig.coaBotDamagePctByClass;
    tables->bySpec = sPlayerbotAIConfig.coaBotDamagePctBySpec;
    tables->vsPlayers = sPlayerbotAIConfig.coaBotDamageVsPlayers;
    bool const active = !tables->byClass.empty() || !tables->bySpec.empty();
    std::atomic_store(&Tables, std::shared_ptr<DamageTables const>(std::move(tables)));
    Active.store(active, std::memory_order_release);
    Loaded.store(true, std::memory_order_release);
}

uint32 DamagePctOf(Unit* attacker, Unit* target)
{
    // First hit after startup: PlayerbotAIConfig::Initialize has run by then.
    if (!Loaded.load(std::memory_order_acquire))
        PublishTables();
    if (!Active.load(std::memory_order_acquire) || !attacker || !target)
        return 100;
    Player* bot = attacker->GetCharmerOrOwnerPlayerOrPlayerItself();
    if (!bot || !GET_PLAYERBOT_AI(bot) || IsRealPlayer(bot))
        return 100;
    // Heals also pass through the periodic hook: only what hits an enemy is scaled.
    if (!attacker->IsHostileTo(target))
        return 100;
    std::shared_ptr<DamageTables const> const tables = std::atomic_load(&Tables);
    if (!tables || (!tables->vsPlayers && target->GetCharmerOrOwnerPlayerOrPlayerItself()))
        return 100;

    auto const spec = tables->bySpec.find(GetAscensionActiveSpecialization(bot));
    if (spec != tables->bySpec.end())
        return spec->second;
    auto const cls = tables->byClass.find(bot->getClass());
    return cls != tables->byClass.end() ? cls->second : 100;
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

// The settings come back with ".reload config": PlayerbotAIConfig::Initialize only runs at startup.
class CoaBotDamageScaleWorldScript : public WorldScript
{
public:
    CoaBotDamageScaleWorldScript() : WorldScript("CoaBotDamageScaleWorldScript", { WORLDHOOK_ON_AFTER_CONFIG_LOAD }) {}

    void OnAfterConfigLoad(bool reload) override
    {
        if (!reload)
            return;
        LoadCoaBotDamageConfig(sPlayerbotAIConfig);
        PublishTables();
    }
};

void AddSC_coa_bot_damage_scale()
{
    new CoaBotDamageScaleUnitScript();
    new CoaBotDamageScaleWorldScript();
}
