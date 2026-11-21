/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_ECONOMY_BOT_AUCTIONS_H
#define COA_ECONOMY_BOT_AUCTIONS_H

#include "ObjectGuid.h"

class Player;

// CoA Economy: random bots use the auction house like players (AiPlayerbot.CoaEconomy.BotAuctions.*, off
// by default). At an auctioneer a bot collects its auction mail (gold of its sales, items it won), lists the loot
// its item usage marks for the auction house, and buys an upgrade (CoA Combat's CoaGearScore::UpgradeGain) or a
// bag it can afford. Everything goes through the core's own auction packets (deposit, cut, mails), so the
// house bot's market learns these sales as real ones.
//
// Threads: the bot's AI runs on its map's thread, the auction house belongs to the world thread. The AI only
// queues a visit; the world thread runs it between two map updates (WorldScript::OnUpdate).
namespace CoaEconomy::BotAuctions
{
    bool IsEnabled();

    // A visit happened less than VisitCooldownMinutes ago.
    bool IsOnCooldown(ObjectGuid bot);

    // Map thread: the bot stands by an auctioneer. False when the visit is not queued (off, cooldown, queue full).
    bool RequestVisit(Player* bot, ObjectGuid auctioneer);

    // 3.2 (BotAuctions.Shop.*): the bot has gold enough to buy an upgrade and its last visit is old enough, so it goes
    // to an auctioneer even with nothing to sell.
    bool WantsToShop(Player* bot);

    // 3.2: its visit is queued and not run yet (City Life keeps the bot by its auctioneer meanwhile: the core's
    // auction handlers refuse a bot more than 5.5 yards away).
    bool HasPendingVisit(ObjectGuid bot);

    // 3.2 (BuyLog): the way of a bot to an auctioneer, before RequestVisit, counted for the hourly "auction visits"
    // line. Sought: its business picked the auctioneer as the next NPC; Picked: a wander took an auctioneer as its
    // target (Station: City Life's pick); GivenUp: the wander dropped it, no walkable way in a capital.
    enum class Way { Sought, Picked, Station, GivenUp };
    void NoteWay(Way way);

    // A bot's trading character, stable per character (GUID hash), each in [0, 1]:
    //   thrift   how little of its gold it spends;
    //   patience how close to the market price it lists (an impatient bot undercuts more).
    // The souls of IA MIND can later replace this hash.
    struct Traits
    {
        float thrift = 0.5f;
        float patience = 0.5f;
    };
    Traits TraitsFor(ObjectGuid guid);
}

#endif
