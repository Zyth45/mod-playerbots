/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_BOT_RACES_H
#define COA_BOT_RACES_H

#include "CoaBotRaceRules.h"

#include <cstdint>

// The races of random bots as the server describes them (AiPlayerbot.BotRace.<id>, CoaBotRaceRules.h). Loaded once at
// startup, before any bot exists and before the map threads start: the profiles are only read afterwards.
namespace CoaBotRaces
{
// Reads every AiPlayerbot.BotRace.<id> line. A line for a race the core does not know, or that does not parse, is
// ignored with an error line; nothing else changes.
void Load();

// The server's profile of the race; the default profile (every field unset) when it wrote none.
CoaBotRace::Profile const& Get(std::uint32_t race);

// The core marks the race male-only (a core that defines COA_CORE_MALE_ONLY_RACES_HEADER); false otherwise.
bool CoreMaleOnly(std::uint8_t race);

// Random bots may use the race: the profile, CoACustomRaces.BotsEnable, ExcludedBotRaces and CrashingBotRaces.
bool Allowed(std::uint32_t race);
}  // namespace CoaBotRaces

#endif
