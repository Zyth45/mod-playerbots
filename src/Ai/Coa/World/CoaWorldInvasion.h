/*
 * CoA World: live invasions (Scourge on the capitals, the Legion at the towns, the gates of Ahn'Qiraj).
 *
 * The event engine (timed events, a notice before, waves summoned from fixed spots, a boss to kill,
 * a reward) is adapted from wow-mod-era-events by buildthehomelab, released under the MIT License:
 *
 *   Copyright (c) 2026 buildthehomelab
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
 *   associated documentation files (the "Software"), to deal in the Software without restriction,
 *   including without limitation the rights to use, copy, modify, merge, publish, distribute,
 *   sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 *   furnished to do so, subject to the following conditions: The above copyright notice and this
 *   permission notice shall be included in all copies or substantial portions of the Software.
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 *
 * CoA changes: no mod-individual-progression (a run belongs to a level band, not an era), no SQL
 * (the Blizzard creatures are summoned as they are and get this module's AI, level and health when
 * they appear), no phasing, and random bots of the band are sent to defend with the players.
 *
 * Part of mod-playerbots (GPL-2.0-or-later).
 */

#ifndef COA_WORLD_INVASION_H
#define COA_WORLD_INVASION_H

#include "Define.h"
#include "ObjectGuid.h"
#include "Position.h"

#include <string>
#include <vector>

class Creature;
class Player;

namespace CoaWorld
{
    enum class SpellTarget : uint8
    {
        Victim,
        Self,
        RandomEnemy,  // a random enemy within 40 yards on the threat list
    };

    struct TimedSpell
    {
        uint32 spellId;
        uint32 minMs;
        uint32 maxMs;
        SpellTarget target;
    };

    // What an invasion creature does, and how strong it is. The Blizzard templates are kept; their
    // own scripts are replaced by the AI of this module while the creature belongs to a run.
    struct MobDef
    {
        uint32 entry = 0;
        float healthMod = 0.0f;   // replaces the template's health modifier, 0 keeps it
        float damageMod = 0.0f;   // replaces the template's damage modifier, 0 keeps it
        int8 levelOffset = 0;     // over the level of the run (bosses +2)
        std::vector<TimedSpell> spells = {};
        uint32 aura = 0;          // kept on the creature, in and out of combat
        uint32 spawnVisual = 0;   // cast once when it appears
        uint32 deathSpell = 0;
        uint32 enrageSpell = 0;
        uint8 enragePct = 0;
        bool seekEnemies = false; // attacks hostile creatures nearby too (armies fighting each other)
    };

    // Registered once at startup; read from any map thread afterwards.
    void RegisterMob(MobDef def);
    MobDef const* FindMob(uint32 entry);

    // Called by the invasion AI from map threads.
    void NotifyMobDied(Creature* creature);

    // Moves a creature to dest, which becomes its home: after a fight on the way it carries on.
    void SendTo(Creature* creature, Position const& dest, bool run = true);

    // Summon context: the creature summoned next on this thread is an invasion creature of that
    // definition, at that level. Set and cleared around Map::SummonCreature by the manager.
    void SetPendingSpawn(MobDef const* def, uint8 level);
    void ClearPendingSpawn();
    void TrackCreature(ObjectGuid guid, MobDef const* def);
    void UntrackCreature(ObjectGuid guid);
    void UntrackAll();

    // Brings the health and damage of a freshly summoned creature to its MobDef.
    void ApplyMobStats(Creature* creature, MobDef const& def, uint32 healthPct);

    // A wave plan: where it happens and what comes through. Data in CoaWorldInvasionEvents.cpp.
    struct InvasionSite
    {
        char const* name;     // "Stormwind", "Gadgetzan"
        char const* zone;     // "Elwynn Forest", "Tanaris"
        uint32 mapId;
        uint32 zoneId;
        uint8 team;           // TEAM_ALLIANCE, TEAM_HORDE, TEAM_NEUTRAL (both)
        Position spawn;       // where the attackers come through
        Position target;      // where they march to
        Position rally;       // where the defenders gather (bots sent from afar arrive here)
    };

    struct InvasionPlan
    {
        char const* key;          // "scourge"
        char const* name;         // "Scourge Invasion"
        char const* configName;   // "Scourge" -> AiPlayerbot.CoaWorld.Invasions.Scourge.*
        uint32 defaultMinLevel;
        uint32 portalObject;      // gameobject placed on the spawn spot, 0 for none
        std::vector<InvasionSite> sites;
        std::vector<uint32> minions;
        std::vector<uint32> elites;   // one per wave from eliteFromWave
        uint32 eliteFromWave;
        uint32 lieutenant;            // comes with lieutenantWave
        uint32 lieutenantWave;
        std::vector<uint32> bosses;   // one of them, after the last wave
        uint32 bossEscort;            // creatures that come with the boss
        uint32 bossEscortCount;
        uint32 defender;              // spawned on the target spot at the start, 0 for none
        uint32 defenderCount;
        uint32 waveCount;
        uint32 waveMs;
        uint32 firstWaveMs;
        uint32 minionsBase;           // per wave: base + wave number
        uint32 maxAlive;
        uint32 rewardFaction;
        uint32 rewardItem;
        uint32 rewardItemCount;
        char const* warningText;      // {0} site, {1} zone, {2} minutes
        char const* startText;        // {0} site, {1} zone
        char const* wonText;          // {0} site
        char const* lostText;         // {0} site
        char const* lieutenantYell;
        char const* bossYell;
    };

    std::vector<InvasionPlan> const& GetInvasionPlans();
    void RegisterInvasionMobs();
}

void AddSC_coa_world_invasions();

#endif
