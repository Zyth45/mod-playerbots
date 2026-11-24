/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAMOVEAUDIT_H
#define PLAYERBOTS_COAMOVEAUDIT_H

#include <cstdint>

class Player;
class PlayerbotAI;

// CoA 3.1.7 (AiPlayerbot.CoaMoveAudit = 0 by default): a night bench of the bots' odd moves. Each bot is sampled
// on its update and a JSON line is written (AiPlayerbot.CoaMoveAudit.File) when it is in the air, on a roof, goes
// through a wall, too fast, teleports or jumps in height, with its last move (action, path type, points, target).
// The fixed NPCs of the capitals far from home and the oversized creatures are written too. Nothing in the game
// changes.
namespace CoaMoveAudit
{
bool Enabled();
// Reads the keys (world start).
void Load();
// On the bot's update (map thread).
void Tick(PlayerbotAI* botAI);
// A move asked by an action (MovementAction::MoveTo): the path the core will get, kept for the next line.
void NoteMove(Player* bot, char const* action, float x, float y, float z);
}  // namespace CoaMoveAudit

void AddSC_coa_move_audit();

#endif
