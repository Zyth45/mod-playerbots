/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_COABOTNAMES_H
#define _PLAYERBOT_COABOTNAMES_H

#include <string>

// The name of a new random bot: "Kegarink Bot" when AiPlayerbot.CoaBotSurname is 1, the first name otherwise
// (mode 2 gives the family name in ApplyCoaBotSurnames, once the bot has a guid).
std::string CoaBotName(std::string const& firstName);

// Gives every random bot the surname of AiPlayerbot.CoaBotSurname (none, "Bot" or its family name), whatever it
// carried before. Run at startup, once the random bots exist and before any of them logs in.
void ApplyCoaBotSurnames();

#endif
