/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_ECONOMY_REAL_PRICES_H
#define COA_ECONOMY_REAL_PRICES_H

#include "Define.h"

struct ItemTemplate;

// CoA Economy: the prices of the real CoA auction house (user, 09/11), from TradeSkillMaster scans of the house
// (CoaRealPrices.inc: 17,121 items and their market value per item). An item absent from the scans gets a floor: the
// median market value of the scanned items of its quality and item level (5-level bands). Read-only, any thread.
namespace CoaEconomy::RealPrices
{
    // Seen on the real house.
    bool Seen(uint32 itemId);
    // Its market value there per item, copper (0 when not seen).
    uint64 Of(uint32 itemId);
    // The floor of its quality and item level (uncommon and better; 0 otherwise or without enough samples).
    uint64 Floor(ItemTemplate const* proto);
    // Of() when seen, else Floor().
    uint64 Reference(ItemTemplate const* proto);
}

#endif
