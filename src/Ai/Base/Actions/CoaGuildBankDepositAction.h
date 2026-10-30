/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAGUILDBANKDEPOSITACTION_H
#define PLAYERBOTS_COAGUILDBANKDEPOSITACTION_H

#include "Action.h"

#include <ctime>

class GameObject;
class Guild;
class Item;
class Player;
class PlayerbotAI;

// AiPlayerbot.GuildBankDeposit.*: a bot of a real player's guild standing near a guild vault puts there the
// items of the configured classes and the gold above what it keeps. The move itself runs in the world thread.
class CoaGuildBankDepositAction : public Action
{
public:
    CoaGuildBankDepositAction(PlayerbotAI* botAI) : Action(botAI, "guild bank deposit") {}

    bool Execute(Event event) override;
    bool isUseful() override;

    // A guild whose leader is not a character of the random bot accounts.
    static bool IsRealPlayerGuild(Guild const* guild);
    // A bag item the bot may put in the guild bank: of a configured class, tradable, never bound, quest or timed.
    static bool IsDepositable(Player* bot, Item const* item);
    static bool HasSomethingToDeposit(Player* bot);
    // A spawned guild vault (not a summoned personal vault) close enough to the bot.
    static GameObject* FindGuildBank(Player* bot);

private:
    time_t nextCheck = 0;
};

#endif
