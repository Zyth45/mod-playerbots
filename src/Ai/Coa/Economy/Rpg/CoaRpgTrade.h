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

// CoA Economy: the "new rpg" random bots stop at vendors to sell their loot and repair, and at
// auctioneers to sell and buy, as people do between two quests (AiPlayerbot.CoaEconomy.RpgSell.* and
// BotAuctions.*, off by default). The new rpg had no such step (RpgSellAction only exists in the old rpg).
// Small hooks in NewRpgAction.cpp / NewRpgBaseAction.cpp call these functions; everything else lives here.
namespace CoaEconomy::RpgTrade
{
    // The bot has business with a vendor (bags filling up, something to sell) or an auctioneer (loot worth an
    // auction, visit not on cooldown).
    bool WantsTrade(PlayerbotAI* botAI);

    // Among the bot's possible rpg targets, the nearest NPC it has business with (0 distanceLimit = any).
    ObjectGuid ChooseTradeNpc(PlayerbotAI* botAI, std::vector<ObjectGuid> const& targets, float distanceLimit = 0.0f);

    // WantsTrade and such an NPC among the bot's possible rpg targets (for the idle step of the new rpg).
    bool HasTradeNpcInReach(PlayerbotAI* botAI);

    // At an NPC in interaction range: sells to a vendor what the bot does not need, repairs at an armorer when its
    // gear is worn, and queues an auction house visit at an auctioneer. True when it did something.
    bool TradeAtNpc(PlayerbotAI* botAI, WorldObject* npc);
}

#endif
