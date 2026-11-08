/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_ECONOMY_DATA_H
#define COA_ECONOMY_DATA_H

#include "Define.h"

#include <string>

// CoA Economy: economy.json for the dashboard (D:\CoA-Dashboard\docs\LIAISON-3.0.md §3 and §6), off unless
// AiPlayerbot.CoaEconomy.DataFile.Enable = 1 and AiPlayerbot.CoaDataFiles is not 0. Built every 60 s on the world
// thread, only while the dashboard watches the "economy" topic (squidbot/watch.json less than 90 s old), and written
// by a thread of its own (.tmp, then renamed). Nothing is built and almost nothing costs while nobody watches.
//
// The bricks of the session report what happens; every call is on the world thread and costs a map insert.
namespace CoaEconomy::Data
{
    // A real auction sale (market prices brick): item, stack size, price paid for the stack.
    void RecordSale(uint32 itemId, uint32 count, uint64 price);

    // An artisan crafted a piece (artisans brick): profession, item, count, profit at market prices.
    void RecordCraft(uint32 skillId, uint32 itemId, uint32 count, int64 profit);

    // A craft order (craft orders brick): "mailed" when sent COD, "refused" when the bot cannot make it, "busy".
    void RecordOrder(std::string const& buyer, std::string const& bot, uint32 itemId, uint32 price, char const* state);
}

void AddSC_coa_economy_data();

#endif
