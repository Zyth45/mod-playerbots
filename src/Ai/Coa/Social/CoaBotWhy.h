/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COABOTWHY_H
#define PLAYERBOTS_COABOTWHY_H

#include <cstdint>
#include <string>

class Player;
class PlayerbotAI;

// CoA 3.2 (AiPlayerbot.CoaWhy.Enable = 1, the user's go of 21/11): ".squidbots why" on a selected bot (or a bot's
// name), for every account (CoaWhy.GmOnly = 1: game masters only), once per CoaWhy.Cooldown seconds per player:
// what the bot is doing (its RPG status and destination, the NPC or object it goes to, the path it would get there,
// its last move and action, how long it has stood still), in the chat and as one line in a file of its own
// (AiPlayerbot.CoaWhy.File) that the player can send us.
namespace CoaBotWhy
{
// What the bot keeps for the command, on its own AI (written on its update, read by the command on the world thread).
struct Trace
{
    std::string moveAction;  // the last move asked by an action
    float moveX = 0.0f, moveY = 0.0f, moveZ = 0.0f;
    uint32_t moveMapId = 0;
    uint64_t moveAtMs = 0;
    float stillX = 0.0f, stillY = 0.0f, stillZ = 0.0f;  // where it stands still since stillSinceMs
    uint32_t stillMapId = 0;
    uint64_t stillSinceMs = 0;
};

bool Enabled();
// On the bot's update (map thread): where it stands still since when.
void Tick(PlayerbotAI* botAI);
// A move asked by an action (MovementAction::MoveTo).
void NoteMove(PlayerbotAI* botAI, std::string const& action, float x, float y, float z);
}  // namespace CoaBotWhy

void AddSC_coa_bot_why();

#endif
