/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAPROFESSIONS_H
#define PLAYERBOTS_COAPROFESSIONS_H

#include "Define.h"

class Player;
struct ItemTemplate;

// CoA 3.3.1: the bots have neither Inscription nor Jewelcrafting (CoaProfessionRules.h says why). A rule of the bots
// only: the players and the core are untouched.
//  - the draw (PlayerbotFactory::InitTradeSkills) never gives them;
//  - a random bot online that has one loses it, with its recipes, wherever it is (out of combat, not in an instance),
//    and gets the profession the draw gives in its place (30 bots a minute at most);
//  - no bot makes, advertises or lists the items only they give (glyphs, inks, pigments, cut gems, jewelry, their
//    recipe items); what the world gives stays (looted scrolls, mined gems, looted Designs). Bot auctions already up
//    run out.
namespace CoaEconomy::Professions
{
// A primary profession the bots no longer have.
bool Retired(uint32 skillId);

// An item only a retired profession gives.
bool IsRetiredItem(ItemTemplate const* proto);

// A random bot that has a retired profession changes it now, if the pace allows (world thread, every scan). True when
// it changed.
bool RetireIfDue(Player* bot);
}

#endif
