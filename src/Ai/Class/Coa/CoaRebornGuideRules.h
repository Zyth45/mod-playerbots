/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAREBORNGUIDERULES_H
#define PLAYERBOTS_COAREBORNGUIDERULES_H

// Warcraft Reborn: who may have its Character Advancement rewritten to the guide build of its tree without asking.
// A random bot and an addclass bot (a server account, account type 2) keep the guide build. A player's own character
// played through the bot AI (an alt brought in by "add") never: its build is the player's. Asked by name ("talents
// spec"), the owner's choice still applies (SetWcrSpec), that is not this rule.
inline bool WcrGuideMayRewrite(bool randomBot, bool addclassBot) { return randomBot || addclassBot; }

#endif
