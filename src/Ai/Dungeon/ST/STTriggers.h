/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef PLAYERBOTS_STTRIGGERS_H
#define PLAYERBOTS_STTRIGGERS_H

#include "Trigger.h"

class Creature;
class GameObject;
class Player;

// Sunken Temple (109), the Avatar of Hakkar in the Sanctum of the Fallen (CoA Combat, guard B5, from Dungeon Clear's
// DcHakkar). While the Shade of Hakkar (8440) is up, or a Hakkari Suppressor (8497) lives within 100 yd:
//   1. a Suppressor still channelling (alive, not yet in combat) is peeled at once: tagging it stops its drain;
//   2. a member carrying Hakkari Blood (10460) douses the nearest of the four Eternal Flames not yet doused
//      (148418-148421); each distinct flame counts once, and the fourth brings the Avatar;
//   3. a member takes the blood from a dead Hakkari Bloodkeeper (8438, a 100 % drop) not yet harvested.
// The run's arrival in the sanctum and the wait for the Avatar stay with the autopilot.
namespace SunkenTemple
{
    constexpr uint32 MAP_ID = 109;
    constexpr uint32 NPC_SHADE_OF_HAKKAR = 8440;
    constexpr uint32 NPC_HAKKARI_SUPPRESSOR = 8497;
    constexpr uint32 NPC_HAKKARI_BLOODKEEPER = 8438;
    constexpr uint32 ITEM_HAKKARI_BLOOD = 10460;
    constexpr uint32 GO_ETERNAL_FLAMES[4] = {148418, 148419, 148420, 148421};
    constexpr float ENCOUNTER_SCAN = 100.0f;
    constexpr float FLAME_USE_RANGE = 5.0f;
    constexpr float LOOT_RANGE = 6.0f;

    // The encounter is live for this bot, and the strategy may act (always in a Dungeon Clear run; outside one when
    // AiPlayerbot.CoaCombat.SunkenTemple.Enable is 1). Out of the encounter it forgets the instance's latches.
    bool HakkarEncounterActs(Player* bot);
    Creature* ChannellingSuppressor(Player* bot);
    GameObject* FlameToDouse(Player* bot);
    Creature* BloodkeeperToHarvest(Player* bot);

    // The per-instance latches, kept under one lock (two Sunken Temple instances tick on different map threads).
    void MarkDoused(Player* bot, GameObject* flame);
    void MarkHarvested(Player* bot, Creature* keeper);
}

class StHakkarSuppressorTrigger : public Trigger
{
public:
    StHakkarSuppressorTrigger(PlayerbotAI* ai) : Trigger(ai, "st hakkar suppressor") {}
    bool IsActive() override;
};

class StHakkarFlameTrigger : public Trigger
{
public:
    StHakkarFlameTrigger(PlayerbotAI* ai) : Trigger(ai, "st hakkar flame") {}
    bool IsActive() override;
};

class StHakkarBloodTrigger : public Trigger
{
public:
    StHakkarBloodTrigger(PlayerbotAI* ai) : Trigger(ai, "st hakkar blood") {}
    bool IsActive() override;
};

#endif
