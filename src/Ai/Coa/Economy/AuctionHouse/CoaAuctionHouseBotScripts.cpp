/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA Economy: the auction house bot (seller and buyer of the house), merged into SquidBot.
// The bot itself is CoaAuctionHouseBot.cpp/.h (from mod-ah-bot-plus by Nathan Handley, GPL-2+).
// This file is SquidBot's own glue: server hooks, mail handling for the house characters and the
// .ahbot commands. Same settings as before (AuctionHouseBot.* in mod_ahbot.conf), and the whole
// thing is only registered when AiPlayerbot.CoaEconomy.AHBot.Enable = 1 (read at start-up).

#include "Chat.h"
#include "CoaAuctionHouseBot.h"
#include "Config.h"
#include "Log.h"
#include "Mail.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WorldSession.h"

namespace
{
    using CoaEconomy::AuctionHouseBot;

    char const* const DropRatesKey = "AuctionHouseBot.AdvancedListingRules.UseDropRates.Enabled";

    bool ReturnsExpiredItemsToHouse()
    {
        return sConfigMgr->GetOption<bool>("AuctionHouseBot.ReturnExpiredAuctionItemsToBot", false);
    }

    // A character the auction house bot plays (configured GUIDs or the automatic character).
    bool IsHouseCharacter(uint32 guidLow)
    {
        for (auto const& houseCharacter : auctionbot->AHCharacters)
            if (houseCharacter.CharacterGUID == guidLow)
                return true;
        return false;
    }

    bool IsHouseCharacter(Player const* player)
    {
        return player && IsHouseCharacter(player->GetGUID().GetCounter());
    }

    // Item lists of the seller, plus the drop rate tables when that rule is on. The key's default
    // depends on the caller: false at start-up and on config load, true for ".ahbot reload".
    void RebuildItemLists(bool dropRatesByDefault)
    {
        auctionbot->PopulateItemCandidatesAndProportions();
        if (sConfigMgr->GetOption<bool>(DropRatesKey, dropRatesByDefault))
        {
            auctionbot->PopulateQuestRewardItemIDs();
            auctionbot->PopulateItemDropChances();
        }
    }

    class CoaAuctionHouseBotWorld : public WorldScript
    {
    public:
        CoaAuctionHouseBotWorld() : WorldScript("CoaAuctionHouseBotWorld") { }

        void OnAfterConfigLoad(bool reload) override
        {
            if (reload)
                auctionbot->EnsureAutoCharacter();
            auctionbot->InitializeConfiguration();

            // The first item lists are built at start-up, once the item templates are loaded.
            if (!_started || !auctionbot->IsModuleEnabled())
                return;

            LOG_INFO("server.loading", "AuctionHouseBot: (Re)populating item candidate lists ...");
            RebuildItemLists(false);
        }

        void OnStartup() override
        {
            _started = true;
            if (!auctionbot->IsModuleEnabled())
                return;

            if (auctionbot->EnsureAutoCharacter())
                auctionbot->InitializeConfiguration();

            LOG_INFO("server.loading", "AuctionHouseBot: (Re)populating item candidate lists ...");
            RebuildItemLists(false);
        }

    private:
        bool _started = false;
    };

    class CoaAuctionHouseBotHouse : public AuctionHouseScript
    {
    public:
        CoaAuctionHouseBotHouse() : AuctionHouseScript("CoaAuctionHouseBotHouse") { }

        // A house character sells: no "sold" toast and no achievement progress for it.
        void OnBeforeAuctionHouseMgrSendAuctionSuccessfulMail(AuctionHouseMgr* /*mgr*/, AuctionEntry* /*auction*/,
            Player* owner, uint32& /*ownerAccount*/, uint32& /*profit*/, bool& sendNotification,
            bool& updateAchievementCriteria, bool& /*sendMail*/) override
        {
            if (!IsHouseCharacter(owner))
                return;
            sendNotification = false;
            updateAchievementCriteria = false;
        }

        // An auction of a house character runs out: silent, and the items come back only if asked.
        void OnBeforeAuctionHouseMgrSendAuctionExpiredMail(AuctionHouseMgr* /*mgr*/, AuctionEntry* /*auction*/,
            Player* owner, uint32& /*ownerAccount*/, bool& sendNotification, bool& sendMail) override
        {
            if (!IsHouseCharacter(owner))
                return;
            sendNotification = false;
            sendMail = ReturnsExpiredItemsToHouse();
        }

        // The house buyer has no session of its own: a player it outbids still hears of it, in its name.
        void OnBeforeAuctionHouseMgrSendAuctionOutbiddedMail(AuctionHouseMgr* /*mgr*/, AuctionEntry* auction,
            Player* oldBidder, uint32& /*oldBidderAccount*/, Player* newBidder, uint32& newPrice,
            bool& /*sendNotification*/, bool& /*sendMail*/) override
        {
            if (!oldBidder || newBidder)
                return;
            oldBidder->GetSession()->SendAuctionBidderNotification(uint32(auction->GetHouseId()), auction->Id,
                ObjectGuid::Create<HighGuid::Player>(auctionbot->CurrentBotCharGUID), newPrice,
                auction->GetAuctionOutBid(), auction->item_template);
        }

        // The house buyer is a shell Player (no map, no achievement data): keep the win away from that state.
        void OnBeforeAuctionHouseMgrSendAuctionWonMail(AuctionHouseMgr* /*mgr*/, AuctionEntry* /*auction*/,
            Player* bidder, uint32& /*bidderAccount*/, bool& sendNotification, bool& updateAchievementCriteria,
            bool& /*sendMail*/) override
        {
            if (!IsHouseCharacter(bidder))
                return;
            sendNotification = false;
            updateAchievementCriteria = false;
        }

        void OnBeforeAuctionHouseMgrUpdate() override { auctionbot->Update(); }
    };

    class CoaAuctionHouseBotMail : public MailScript
    {
    public:
        CoaAuctionHouseBotMail() : MailScript("CoaAuctionHouseBotMail") { }

        // Mail to a house character: delivered only when expired items go back to it; otherwise
        // dropped, and the items of an auction mail are deleted with it.
        void OnBeforeMailDraftSendMailTo(MailDraft* /*draft*/, MailReceiver const& receiver, MailSender const& sender,
            MailCheckMask& /*checked*/, uint32& /*deliverDelay*/, uint32& /*customExpiration*/,
            bool& deleteMailItemsFromDB, bool& sendMail) override
        {
            if (!IsHouseCharacter(receiver.GetPlayerGUIDLow()))
                return;

            if (ReturnsExpiredItemsToHouse())
            {
                deleteMailItemsFromDB = false;
                sendMail = true;
                return;
            }

            if (sender.GetMailMessageType() == MAIL_AUCTION)
                deleteMailItemsFromDB = true;
            sendMail = false;
        }
    };

    class CoaAuctionHouseBotCommands : public CommandScript
    {
    public:
        CoaAuctionHouseBotCommands() : CommandScript("CoaAuctionHouseBotCommands") { }

        Acore::ChatCommands::ChatCommandTable GetCommands() const override
        {
            using namespace Acore::ChatCommands;
            static ChatCommandTable ahbot = {
                {"update", HandleUpdate, SEC_GAMEMASTER, Console::Yes},
                {"reload", HandleReload, SEC_GAMEMASTER, Console::Yes},
                {"empty", HandleEmpty, SEC_GAMEMASTER, Console::Yes},
                {"help", HandleHelp, SEC_GAMEMASTER, Console::Yes},
            };
            static ChatCommandTable root = {
                {"ahbot", ahbot},
            };
            return root;
        }

    private:
        static void Say(ChatHandler* handler, char const* text)
        {
            LOG_INFO("module", "{}", text);
            handler->PSendSysMessage("{}", text);
        }

        static bool HandleUpdate(ChatHandler* handler, char const* /*args*/)
        {
            Say(handler, "AuctionHouseBot: Updating Auction House...");
            AuctionHouseBot::instance()->Update();
            Say(handler, "AuctionHouseBot: Auction House Updated.");
            return true;
        }

        static bool HandleReload(ChatHandler* handler, char const* /*args*/)
        {
            Say(handler, "AuctionHouseBot: Reloading Config...");
            sConfigMgr->LoadModulesConfigs(true, false);
            AuctionHouseBot::instance()->EnsureAutoCharacter();
            AuctionHouseBot::instance()->InitializeConfiguration();
            RebuildItemLists(true);
            Say(handler, "AuctionHouseBot: Config reloaded.");
            return true;
        }

        static bool HandleEmpty(ChatHandler* handler, char const* /*args*/)
        {
            Say(handler, "AuctionHouseBot: Emptying Auction House...");
            AuctionHouseBot::instance()->EmptyAuctionHouses();
            // Only once the auctions are gone.
            AuctionHouseBot::instance()->CleanupExpiredAuctionItems();
            Say(handler, "AuctionHouseBot: Auction Houses Emptied.");
            return true;
        }

        static bool HandleHelp(ChatHandler* handler, char const* /*args*/)
        {
            handler->PSendSysMessage("AuctionHouseBot commands:");
            handler->PSendSysMessage("  .ahbot reload - Reloads configuration");
            handler->PSendSysMessage("  .ahbot empty  - Removes all AuctionHouseBot auctions");
            handler->PSendSysMessage("  .ahbot update - Runs an update cycle");
            return true;
        }
    };
}

void AddSC_coa_economy_ahbot()
{
    // Off by default: while mod-ah-bot-plus is still built into the core, only one of the two may run.
    if (!sConfigMgr->GetOption<bool>("AiPlayerbot.CoaEconomy.AHBot.Enable", false, false))
        return;

    LOG_INFO("server.loading", "CoA Economy: auction house bot merged into SquidBot is on");
    new CoaAuctionHouseBotWorld();
    new CoaAuctionHouseBotHouse();
    new CoaAuctionHouseBotMail();
    new CoaAuctionHouseBotCommands();
}
