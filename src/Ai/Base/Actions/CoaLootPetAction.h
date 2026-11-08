/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COALOOTPETACTION_H
#define PLAYERBOTS_COALOOTPETACTION_H

#include "Action.h"

class Creature;
class Player;
class PlayerbotAI;

// "loot pet [list | <spell id or link> | off | auto]", whispered to an alt by its owner or sent by the SquidBots
// addon: the alt's loot pet (AiPlayerbot.AltLootCompanion, CoaLootCompanionRules.h). Answers the LP lines to the
// addon, a short sentence otherwise.
class CoaLootPetAction : public Action
{
public:
    CoaLootPetAction(PlayerbotAI* botAI) : Action(botAI, "loot pet") {}

    bool Execute(Event event) override;
};

// Run by the bot's own update before its engine: an alt with AiPlayerbot.AltLootCompanion brings out its loot pet.
void CoaLootCompanionTick(PlayerbotAI* botAI);

// Whether the alt leaves this corpse's loot (or its skinning) to its companion instead of walking to it.
bool CoaLeavesCorpseToCompanion(Player* bot, Creature* corpse, bool skinning);

#endif
