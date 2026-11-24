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
// 3.2.2: straight: walked in a straight line (no path from the core, CoaSafeMove's flat line).
void NoteMove(Player* bot, char const* action, float x, float y, float z, bool straight = false);
// 3.2.2: a move that is not an action's MoveTo (a teleport off a navmesh island...), kept as the move's origin.
void NoteOrigin(Player* bot, char const* origin);
// 3.2.2 (Pilote, bench of 1e991b2d: teleport 5 686, wall 4 944 an hour): a teleport by the server (TeleportTo,
// NearTeleportTo: a random teleport, a hearth, an unstick), from the core's OnPlayerBeforeTeleport. The sample that spans
// it is no anomaly (teleport, fast, wall, zjump, terrain).
void NoteTeleport(Player* bot);
}  // namespace CoaMoveAudit

void AddSC_coa_move_audit();

#endif
