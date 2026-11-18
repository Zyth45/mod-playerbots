/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COATRUST_H
#define PLAYERBOTS_COATRUST_H

#include <cstdint>
#include <string>

class Player;
class PlayerbotAI;
class Unit;

// CoA 3.1.6 (AiPlayerbot.CoaTrust.Enable): the trust of a player in each of his bots, alts or recruited. It grows
// with the monsters killed together, more for a boss (CoaTrustRules), and is kept in the table playerbots_coa_trust of the
// playerbots database. Shown only, in the addon's Info tab: "trust" whispered on SQBA by the bot's master, answered
// with the line of CoaTrustRules::Line. No rank opens or locks anything.
namespace CoaTrust
{
// Loads the table (world start).
void Load();
// A creature killed with credit to player (the core's kill hook): every bot of the group with its master there
// gains, for that master.
void OnKill(Player* player, Unit* victim);
// The changed scores written to the table (world thread, every minute and at shutdown).
void Flush(uint32_t diff, bool now = false);
// The command of the queue (CoaAltBags::Command): true when it was "trust".
bool Command(PlayerbotAI* botAI, std::string const& command, std::string const& arg);
}  // namespace CoaTrust

#endif
