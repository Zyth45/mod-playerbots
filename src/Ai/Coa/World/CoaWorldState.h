/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA World: monde.json for the CoA Dashboard, the current state of the session's systems (schema
// "squidbot.monde/1"): invasions, rushes, guilds, guild orders, server firsts, recovery over 24 h. Every 30 s, only
// while squidbot/watch.json asks for "monde" (less than 90 s old). events.jsonl keeps the history.
// Off by default: AiPlayerbot.CoaWorld.StateFile.Enable = 1, and AiPlayerbot.CoaDataFiles not 0.

#ifndef PLAYERBOTS_COAWORLDSTATE_H
#define PLAYERBOTS_COAWORLDSTATE_H

#include "Define.h"

#include <ctime>
#include <string>

namespace CoaWorldState
{
// Kept only while the file is on; cheap no-ops otherwise. Any thread.
void Order(std::string const& guild, uint32 item, uint32 count, char const* state, std::string const& by);
void Recovery(bool unstuck);
void First(std::string const& what, std::string const& by, std::string const& guild, time_t at);
}  // namespace CoaWorldState

// Built by their own bricks on the world thread: JSON arrays.
namespace CoaWorld
{
std::string InvasionsJson(time_t now);
std::string RushesJson(time_t now);
}  // namespace CoaWorld

void AddSC_coa_world_state();

#endif
