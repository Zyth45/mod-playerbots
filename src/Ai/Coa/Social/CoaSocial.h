/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COASOCIAL_H
#define PLAYERBOTS_COASOCIAL_H

class PlayerbotAI;
class Unit;

// CoA Live, L2 social bots: what a random bot can cast on a friendly unit outside its group.
enum class CoaAllyCast
{
    Buff,
    Heal,
    Resurrect,
};

// One spell of that kind the bot knows, cast on the target now (CoaAiObjectContext.cpp). False when none fits.
bool CoaCastOnAlly(PlayerbotAI* botAI, Unit* target, CoaAllyCast what);

#endif
