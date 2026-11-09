/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA World: every script of the session is registered here, in this order.
// Called once by AddPlayerbotsScripts (src/Script/Playerbots.cpp); a new script of the session goes here.

#include "CoaWorldInvasion.h"

void AddSC_coa_world_rush();
void AddSC_coa_world_dungeon_sim();
void AddSC_coa_world_pvp();
void AddSC_coa_world_recovery();
void AddSC_coa_world_travel();
void AddSC_coa_world_server_first();
void AddSC_coa_world_guilds();
void AddSC_coa_world_density();
void AddSC_coa_world_events();
void AddSC_coa_world_leveling();
void AddSC_coa_world_state();
void AddSC_coa_world_addon();
void AddSC_coa_world_bg_pace();
void AddSC_coa_world_bg_sim();
void AddSC_coa_world_charters();
void AddSC_coa_world_guild_presence();
void AddSC_coa_world_ai_budget();

void AddCoaWorldScripts()
{
    AddSC_coa_world_state();  // first: its start-up is read by the others' (ServerFirst)
    AddSC_coa_world_invasions();
    AddSC_coa_world_rush();
    AddSC_coa_world_dungeon_sim();
    AddSC_coa_world_pvp();
    AddSC_coa_world_recovery();
    AddSC_coa_world_travel();
    AddSC_coa_world_server_first();
    AddSC_coa_world_guilds();
    AddSC_coa_world_density();
    AddSC_coa_world_events();
    AddSC_coa_world_leveling();
    AddSC_coa_world_addon();
    AddSC_coa_world_bg_pace();
    AddSC_coa_world_bg_sim();
    AddSC_coa_world_charters();
    AddSC_coa_world_guild_presence();
    AddSC_coa_world_ai_budget();
}
