/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_GARDES_SETTINGS_H
#define COA_GARDES_SETTINGS_H

// The tunables of the party combat guards, each read as AiPlayerbot.<Key> with the player's old DungeonClear.<Key>
// first (CoaGardesConfig). Defaults are Dungeon Clear's registry defaults. The guards read them every tick, and a
// configuration lookup walks every key, so the values are kept in one snapshot refreshed every few seconds (a
// `.reload config` is picked up within that).
struct CoaGardesSettings
{
    bool combatRegroup = true;          // CombatRegroup
    float combatRegroupDistance = 40.0f; // CombatRegroupDistance: the hard outer tether
    float combatRegroupSlack = 8.0f;    // CombatRegroupSlack: healer pre-position band under heal range
    float combatRegroupCooldown = 5.0f; // CombatRegroupCooldown (s)
    float healRepositionHpFloor = 90.0f; // HealRepositionHpFloor: a member under it is a hurt heal target
    float roomAggroPathPadding = 3.0f;  // RoomAggroPathPadding: the room boss wake sphere's party buffer
    float aggroRangeMargin = 2.0f;      // AggroRangeMargin

    static CoaGardesSettings Get();
};

#endif
