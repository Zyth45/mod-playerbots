/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_ECONOMY_CHATTER_H
#define COA_ECONOMY_CHATTER_H

class Player;

// CoA Economy: a bot's words about its trade, said the CoA Live way (bank lines, typing delay).
// ah_sold: now and then, a bot whose auction just sold says so in Trade (AiPlayerbot.CoaEconomy.Chatter.AhSold.*).
// trade_window: a real player opens a trade window with a random bot it does not trade with; the bot answers with a
// polite whisper instead of "I'm kind of busy now" (AiPlayerbot.CoaEconomy.Chatter.TradeWindow.*).
namespace CoaEconomy::Chatter
{
    // TradeStatusAction, before it cancels a trade a real player opened: true when the bot said trade_window (the
    // caller then says nothing else); false: the caller keeps its own line. Paced per player.
    bool OnTradeRefused(Player* bot, Player* trader);
}

#endif
