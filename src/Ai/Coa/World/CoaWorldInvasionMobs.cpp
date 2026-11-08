/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

/*
 * CoA World: the AI, level and strength of invasion creatures.
 *
 * Adapted from wow-mod-era-events (EraEventMobs.cpp) by buildthehomelab, MIT License; see
 * CoaWorldInvasion.h for the notice. On CoA no creature_template is copied: the Blizzard creature
 * is summoned as it is, and two AllCreatureScript hooks give it the run's level (before its stats
 * are rolled) and this module's AI (before its own script starts) while the summon is in progress.
 *
 * Part of mod-playerbots (GPL-2.0-or-later).
 */

#include "CoaWorldInvasion.h"

#include "AllCreatureScript.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "MotionMaster.h"
#include "ScriptMgr.h"
#include "ScriptedCreature.h"

#include <atomic>
#include <mutex>
#include <unordered_map>

namespace CoaWorld
{
    namespace
    {
        constexpr uint32 POINT_ARRIVE = 1;

        std::unordered_map<uint32, MobDef>& Defs()
        {
            static std::unordered_map<uint32, MobDef> defs;
            return defs;
        }

        // The summon in progress on this thread (world thread only, but the hooks are called from
        // whatever thread creates the creature, so it is thread-local).
        thread_local MobDef const* t_pendingDef = nullptr;
        thread_local uint8 t_pendingLevel = 0;

        // Creatures of a running invasion, for an AI reset after the first one (rare: charm,
        // UpdateEntry). Empty and skipped while nothing runs.
        std::mutex g_trackedLock;
        std::unordered_map<ObjectGuid, MobDef const*> g_tracked;
        std::atomic<bool> g_anyTracked{false};

        MobDef const* FindTracked(ObjectGuid guid)
        {
            if (!g_anyTracked.load(std::memory_order_relaxed))
                return nullptr;
            std::lock_guard<std::mutex> lock(g_trackedLock);
            auto itr = g_tracked.find(guid);
            return itr != g_tracked.end() ? itr->second : nullptr;
        }

        void CastTimedSpell(ScriptedAI* ai, TimedSpell const& spell)
        {
            switch (spell.target)
            {
                case SpellTarget::Victim:
                    ai->DoCastVictim(spell.spellId);
                    break;
                case SpellTarget::Self:
                    ai->DoCastSelf(spell.spellId);
                    break;
                case SpellTarget::RandomEnemy:
                    if (Unit* target = ai->SelectTarget(SelectTargetMethod::Random, 0, 40.0f, true))
                        ai->DoCast(target, spell.spellId);
                    break;
            }
        }
    }

    void RegisterMob(MobDef def)
    {
        uint32 entry = def.entry;
        Defs()[entry] = std::move(def);
    }

    MobDef const* FindMob(uint32 entry)
    {
        auto itr = Defs().find(entry);
        return itr != Defs().end() ? &itr->second : nullptr;
    }

    void SetPendingSpawn(MobDef const* def, uint8 level)
    {
        t_pendingDef = def;
        t_pendingLevel = level;
    }

    void ClearPendingSpawn()
    {
        t_pendingDef = nullptr;
        t_pendingLevel = 0;
    }

    void TrackCreature(ObjectGuid guid, MobDef const* def)
    {
        std::lock_guard<std::mutex> lock(g_trackedLock);
        g_tracked[guid] = def;
        g_anyTracked.store(true, std::memory_order_relaxed);
    }

    void UntrackCreature(ObjectGuid guid)
    {
        std::lock_guard<std::mutex> lock(g_trackedLock);
        g_tracked.erase(guid);
        g_anyTracked.store(!g_tracked.empty(), std::memory_order_relaxed);
    }

    void UntrackAll()
    {
        std::lock_guard<std::mutex> lock(g_trackedLock);
        g_tracked.clear();
        g_anyTracked.store(false, std::memory_order_relaxed);
    }

    void SendTo(Creature* creature, Position const& dest, bool run)
    {
        if (!creature)
            return;

        // Home is where it's headed, so a fight on the way ends with it carrying on there.
        creature->SetHomePosition(dest);
        creature->SetWalk(!run);
        creature->GetMotionMaster()->MovePoint(POINT_ARRIVE, dest);
    }

    void ApplyMobStats(Creature* creature, MobDef const& def, uint32 healthPct)
    {
        CreatureTemplate const* cInfo = creature->GetCreatureTemplate();
        if (!cInfo)
            return;

        // Health: the core rolled it with the template's modifier at the run's level.
        float healthRatio = (def.healthMod > 0.0f && cInfo->ModHealth > 0.0f) ? def.healthMod / cInfo->ModHealth : 1.0f;
        healthRatio *= float(std::max<uint32>(10, healthPct)) / 100.0f;
        if (healthRatio != 1.0f)
        {
            uint32 health = std::max<uint32>(1, uint32(float(creature->GetCreateHealth()) * healthRatio));
            creature->SetCreateHealth(health);
            creature->SetStatFlatModifier(UNIT_MOD_HEALTH, BASE_VALUE, float(health));
            creature->UpdateMaxHealth();
            creature->SetFullHealth();
        }

        // Damage: the template's modifier is applied at every damage update; a base percent offsets it.
        if (def.damageMod > 0.0f && cInfo->DamageModifier > 0.0f)
        {
            float damageRatio = def.damageMod / cInfo->DamageModifier;
            creature->SetStatPctModifier(UNIT_MOD_DAMAGE_MAINHAND, BASE_PCT, damageRatio);
            creature->SetStatPctModifier(UNIT_MOD_DAMAGE_OFFHAND, BASE_PCT, damageRatio);
            creature->SetStatPctModifier(UNIT_MOD_DAMAGE_RANGED, BASE_PCT, damageRatio);
            creature->UpdateDamagePhysical(BASE_ATTACK);
            creature->UpdateDamagePhysical(OFF_ATTACK);
            creature->UpdateDamagePhysical(RANGED_ATTACK);
        }
    }
}

using namespace CoaWorld;

// The AI of every invasion creature: its spells in combat, the march to where the run sends it,
// fights with the other side's army, and the death report to the manager.
struct npc_coa_world_invader : public ScriptedAI
{
    npc_coa_world_invader(Creature* creature, MobDef const* def) : ScriptedAI(creature), _def(def) { }

    void InitializeAI() override
    {
        if (_def && _def->spawnVisual)
            DoCastSelf(_def->spawnVisual, true);

        ScriptedAI::InitializeAI();
    }

    void Reset() override
    {
        scheduler.CancelAll();
        _enraged = false;

        if (_def && _def->aura && !me->HasAura(_def->aura))
            me->AddAura(_def->aura, me);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        if (!_def)
            return;

        for (TimedSpell const& spell : _def->spells)
        {
            scheduler.Schedule(Milliseconds(urand(spell.minMs / 2, spell.maxMs)), [this, spell](TaskContext context)
            {
                CastTimedSpell(this, spell);
                context.Repeat(Milliseconds(urand(spell.minMs, spell.maxMs)));
            });
        }
    }

    // Holds the spot it was sent to instead of running back to where it was summoned.
    void MovementInform(uint32 type, uint32 id) override
    {
        if (type == POINT_MOTION_TYPE && id == POINT_ARRIVE)
            me->SetHomePosition(me->GetPosition());
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (_def && _def->deathSpell)
            DoCastSelf(_def->deathSpell, true);

        NotifyMobDied(me);
    }

    void UpdateAI(uint32 diff) override
    {
        // Armies fight each other, not only players: look for the nearest enemy now and then.
        if (_def && _def->seekEnemies && !me->IsInCombat())
        {
            if (_seekTimer <= diff)
            {
                _seekTimer = 2 * IN_MILLISECONDS;
                if (Unit* target = me->SelectNearestTarget(30.0f))
                    AttackStart(target);
            }
            else
                _seekTimer -= diff;
        }

        if (!UpdateVictim())
            return;

        if (_def && _def->enrageSpell && !_enraged && HealthBelowPct(_def->enragePct))
        {
            _enraged = true;
            DoCastSelf(_def->enrageSpell, true);
        }

        scheduler.Update(diff);
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        DoMeleeAttackIfReady();
    }

private:
    MobDef const* _def;
    bool _enraged = false;
    uint32 _seekTimer = 0;
};

class CoaWorldInvaderScript : public AllCreatureScript
{
public:
    CoaWorldInvaderScript() : AllCreatureScript("CoaWorldInvaderScript") { }

    // Before the stats are rolled: the run's level instead of the template's (WotLK levels 69-72).
    void OnBeforeCreatureSelectLevel(CreatureTemplate const* /*cinfo*/, Creature* /*creature*/, uint8& level) override
    {
        if (t_pendingDef && t_pendingLevel)
            level = t_pendingLevel;
    }

    // Before the creature's own script starts (the Scourge Invasion scripts write the core's
    // world state, the Qiraji ones run the AQ war): this module's AI instead.
    CreatureAI* GetCreatureAI(Creature* creature) const override
    {
        if (t_pendingDef && t_pendingDef->entry == creature->GetEntry())
            return new npc_coa_world_invader(creature, t_pendingDef);

        if (MobDef const* def = FindTracked(creature->GetGUID()))
            return new npc_coa_world_invader(creature, def);

        return nullptr;
    }
};

void AddSC_coa_world_invader_ai()
{
    new CoaWorldInvaderScript();
}
