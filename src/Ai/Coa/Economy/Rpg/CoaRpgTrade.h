/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_ECONOMY_RPG_TRADE_H
#define COA_ECONOMY_RPG_TRADE_H

#include "ObjectGuid.h"

#include <vector>

class PlayerbotAI;
class WorldObject;

// CoA Economy (lot 3): the "new rpg" random bots stop at vendors to sell their loot and repair, as people
// do between two quests (AiPlayerbot.CoaEconomy.RpgSell.*, off by default). The new rpg had no such step
// (RpgSellAction only exists in the old rpg). Small hooks in NewRpgAction.cpp / NewRpgBaseAction.cpp call
// these functions; everything else lives here.
namespace CoaEconomy::RpgTrade
{
    // Bags filled to the threshold and something to sell: the bot looks for a vendor.
    bool WantsVendor(PlayerbotAI* botAI);

    // A vendor among the bot's possible rpg targets, the nearest one within distanceLimit (0 = any).
    ObjectGuid ChooseVendor(PlayerbotAI* botAI, std::vector<ObjectGuid> const& targets, float distanceLimit = 0.0f);

    // At an NPC in interaction range: sells what the bot does not need to a vendor, and repairs at an
    // armorer when its gear is worn. True when it did something.
    bool TradeAtNpc(PlayerbotAI* botAI, WorldObject* npc);
}

#endif
