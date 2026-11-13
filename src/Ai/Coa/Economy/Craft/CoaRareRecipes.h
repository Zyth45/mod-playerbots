/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_ECONOMY_RARE_RECIPES_H
#define COA_ECONOMY_RARE_RECIPES_H

class Player;

// CoA Economy: artisans' rare recipes (AiPlayerbot.CoaEconomy.Artisans.LearnRareRecipes.*, on by default). Bots
// learn only what profession trainers teach; with this on, a random bot artisan also knows a few of the recipes a
// recipe item in the world teaches (dropped or sold by a vendor), up to its rank and level, that make a tradable item
// of its era. Each recipe is drawn once per bot, so an artisan keeps the same recipes and learns more as it rises.
namespace CoaEconomy::RareRecipes
{
    bool IsEnabled();
    void LoadSettings();

    // Teaches the bot the rare recipes it has come to know since the last call (nothing when its ranks and level are
    // unchanged). Random bots only; safe from any map thread.
    void Maintain(Player* bot);
}

void AddSC_coa_economy_rare_recipes();

#endif
