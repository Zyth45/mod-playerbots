/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_BOT_CONTROL_RULES_H
#define COA_BOT_CONTROL_RULES_H

// Whether a character may be logged in as a bot of a player, apart from the game objects, so that
// tests/coa_bot_control_rules.cpp can check it: a random bot load (no master account), a character of the player's
// account (AllowAccountBots), of the player's guild (AllowGuildBots), an addclass bot, or one of a linked account
// (AllowTrustedAccountBots). The "add" command and the bot loader both ask it, so "add: X - ok" is never answered for
// a character the loader then refuses.
inline bool CoaMayControlBot(bool randomBotLoad, bool sameAccount, bool sameGuild, bool addClassBot, bool linkedAccount)
{
    return randomBotLoad || sameAccount || sameGuild || addClassBot || linkedAccount;
}

#endif
