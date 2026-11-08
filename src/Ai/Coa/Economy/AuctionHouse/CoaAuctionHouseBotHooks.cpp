/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA Economy: wires the GPL auction house bot (CoaAuctionHouseBot.h, namespace CoaEconomy,
// macro `auctionbot`) to server events. Written from a behaviour description.

#include "AuctionHouseMgr.h"
#include "Chat.h"
#include "CoaAuctionHouseBot.h"
#include "Config.h"
#include "Log.h"
#include "Mail.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WorldSession.h"

using namespace Acore::ChatCommands;

namespace
{

constexpr char const* KEY_USE_DROP_RATES = "AuctionHouseBot.AdvancedListingRules.UseDropRates.Enabled";
constexpr char const* KEY_RETURN_EXPIRED = "AuctionHouseBot.ReturnExpiredAuctionItemsToBot";
constexpr char const* KEY_MODULE_ENABLE = "AiPlayerbot.CoaEconomy.AHBot.Enable";

// Set once the world startup hook has run.
bool gStartupDone = false;

bool UseDropRates()
{
    return sConfigMgr->GetOption<bool>(KEY_USE_DROP_RATES, false);
}

bool ReturnExpiredToBot()
{
    return sConfigMgr->GetOption<bool>(KEY_RETURN_EXPIRED, false);
}

// True when the low guid belongs to one of the bot's own characters.
bool IsBotCharacter(ObjectGuid::LowType guidLow)
{
    for (CoaEconomy::AuctionHouseBotCharacter const& character : auctionbot->AHCharacters)
        if (character.CharacterGUID == guidLow)
            return true;
    return false;
}

bool IsBotCharacter(Player const* player)
{
    return player && IsBotCharacter(player->GetGUID().GetCounter());
}

// Rebuild the item pools the bot sells from, plus drop-rate data when enabled.
void RebuildItemData()
{
    auctionbot->PopulateItemCandidatesAndProportions();
    if (UseDropRates())
    {
        auctionbot->PopulateQuestRewardItemIDs();
        auctionbot->PopulateItemDropChances();
    }
}

class CoaAhBotWorldHooks : public WorldScript
{
public:
    CoaAhBotWorldHooks() : WorldScript("CoaAhBotWorldHooks", {WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP}) { }

    void OnAfterConfigLoad(bool reload) override
    {
        if (reload)
            auctionbot->EnsureAutoCharacter();

        auctionbot->InitializeConfiguration();
        if (!auctionbot->IsModuleEnabled())
            return;

        // Item data needs the item store, so only rebuild here once the server is up.
        if (gStartupDone)
            RebuildItemData();
    }

    void OnStartup() override
    {
        if (auctionbot->IsModuleEnabled())
        {
            if (auctionbot->EnsureAutoCharacter())
                auctionbot->InitializeConfiguration();
            RebuildItemData();
        }
        gStartupDone = true;
    }
};

class CoaAhBotAuctionHooks : public AuctionHouseScript
{
public:
    CoaAhBotAuctionHooks() : AuctionHouseScript("CoaAhBotAuctionHooks", {
        AUCTIONHOUSEHOOK_ON_BEFORE_AUCTIONHOUSEMGR_SEND_AUCTION_SUCCESSFUL_MAIL,
        AUCTIONHOUSEHOOK_ON_BEFORE_AUCTIONHOUSEMGR_SEND_AUCTION_EXPIRED_MAIL,
        AUCTIONHOUSEHOOK_ON_BEFORE_AUCTIONHOUSEMGR_SEND_AUCTION_WON_MAIL,
        AUCTIONHOUSEHOOK_ON_BEFORE_AUCTIONHOUSEMGR_SEND_AUCTION_OUTBIDDED_MAIL,
        AUCTIONHOUSEHOOK_ON_BEFORE_AUCTIONHOUSEMGR_UPDATE}) { }

    // Bot sold something: keep it silent.
    void OnBeforeAuctionHouseMgrSendAuctionSuccessfulMail(AuctionHouseMgr* /*mgr*/, AuctionEntry* /*auction*/, Player* owner,
        uint32& /*ownerAccId*/, uint32& /*profit*/, bool& sendNotification, bool& updateAchievementCriteria, bool& /*sendMail*/) override
    {
        if (!IsBotCharacter(owner))
            return;
        sendNotification = false;
        updateAchievementCriteria = false;
    }

    // Bot listing expired: silent, items come back only if configured.
    void OnBeforeAuctionHouseMgrSendAuctionExpiredMail(AuctionHouseMgr* /*mgr*/, AuctionEntry* /*auction*/, Player* owner,
        uint32& /*ownerAccId*/, bool& sendNotification, bool& sendMail) override
    {
        if (!IsBotCharacter(owner))
            return;
        sendNotification = false;
        if (!ReturnExpiredToBot())
            sendMail = false;
    }

    // Bot won an auction: keep it silent.
    void OnBeforeAuctionHouseMgrSendAuctionWonMail(AuctionHouseMgr* /*mgr*/, AuctionEntry* /*auction*/, Player* bidder,
        uint32& /*bidderAccId*/, bool& sendNotification, bool& updateAchievementCriteria, bool& /*sendMail*/) override
    {
        if (!IsBotCharacter(bidder))
            return;
        sendNotification = false;
        updateAchievementCriteria = false;
    }

    // A real player outbid by the bot (no bidder object): the core skips the
    // notification, so send it here with the bot's current character as bidder.
    void OnBeforeAuctionHouseMgrSendAuctionOutbiddedMail(AuctionHouseMgr* /*mgr*/, AuctionEntry* auction, Player* oldBidder,
        uint32& /*oldBidderAccId*/, Player* newBidder, uint32& newPrice, bool& /*sendNotification*/, bool& /*sendMail*/) override
    {
        if (!auction || !oldBidder || newBidder || IsBotCharacter(oldBidder))
            return;

        WorldSession* session = oldBidder->GetSession();
        if (!session)
            return;

        ObjectGuid botGuid = ObjectGuid::Create<HighGuid::Player>(auctionbot->CurrentBotCharGUID);
        session->SendAuctionBidderNotification(uint32(auction->GetHouseId()), auction->Id, botGuid, newPrice,
            auction->GetAuctionOutBid(), auction->item_template);
    }

    void OnBeforeAuctionHouseMgrUpdate() override
    {
        auctionbot->Update();
    }
};

class CoaAhBotMailHooks : public MailScript
{
public:
    CoaAhBotMailHooks() : MailScript("CoaAhBotMailHooks", {MAILHOOK_ON_BEFORE_MAIL_DRAFT_SEND_MAIL_TO}) { }

    // Mail to a bot character: deliver only if expired items go back to the bot.
    // Otherwise drop it, and delete auction mail items from the database.
    void OnBeforeMailDraftSendMailTo(MailDraft* /*draft*/, MailReceiver const& receiver, MailSender const& sender,
        MailCheckMask& /*checked*/, uint32& /*deliverDelay*/, uint32& /*customExpiration*/, bool& deleteMailItemsFromDB,
        bool& sendMail) override
    {
        if (!IsBotCharacter(receiver.GetPlayerGUIDLow()))
            return;

        if (ReturnExpiredToBot())
        {
            sendMail = true;
            deleteMailItemsFromDB = false;
            return;
        }

        sendMail = false;
        if (sender.GetMailMessageType() == MAIL_AUCTION)
            deleteMailItemsFromDB = true;
    }
};

class CoaAhBotCommands : public CommandScript
{
public:
    CoaAhBotCommands() : CommandScript("CoaAhBotCommands") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable ahbotTable = {
            {"update", HandleUpdate, SEC_GAMEMASTER, Console::Yes},
            {"reload", HandleReload, SEC_GAMEMASTER, Console::Yes},
            {"empty", HandleEmpty, SEC_GAMEMASTER, Console::Yes},
            {"help", HandleHelp, SEC_GAMEMASTER, Console::Yes},
        };
        static ChatCommandTable rootTable = {
            {"ahbot", ahbotTable},
        };
        return rootTable;
    }

    static bool HandleUpdate(ChatHandler* handler)
    {
        LOG_INFO("module", "CoA Economy AH bot: manual update cycle requested.");
        handler->SendSysMessage("AH bot: running one update cycle.");
        auctionbot->Update();
        return true;
    }

    static bool HandleReload(ChatHandler* handler)
    {
        LOG_INFO("module", "CoA Economy AH bot: reloading configuration.");
        handler->SendSysMessage("AH bot: reloading configuration and item data.");
        sConfigMgr->LoadModulesConfigs(true, false);
        auctionbot->EnsureAutoCharacter();
        auctionbot->InitializeConfiguration();
        RebuildItemData();
        return true;
    }

    static bool HandleEmpty(ChatHandler* handler)
    {
        LOG_INFO("module", "CoA Economy AH bot: removing all bot auctions.");
        handler->SendSysMessage("AH bot: removing all bot auctions and cleaning expired items.");
        auctionbot->EmptyAuctionHouses();
        auctionbot->CleanupExpiredAuctionItems();
        return true;
    }

    static bool HandleHelp(ChatHandler* handler)
    {
        LOG_INFO("module", "CoA Economy AH bot: help shown.");
        handler->SendSysMessage("AH bot commands:");
        handler->SendSysMessage("  .ahbot update - run one bot update cycle now");
        handler->SendSysMessage("  .ahbot reload - reload config and rebuild item data");
        handler->SendSysMessage("  .ahbot empty  - remove all bot auctions");
        return true;
    }
};

} // namespace

void AddSC_coa_economy_ahbot()
{
    if (!sConfigMgr->GetOption<bool>(KEY_MODULE_ENABLE, false))
        return;

    new CoaAhBotWorldHooks();
    new CoaAhBotAuctionHooks();
    new CoaAhBotMailHooks();
    new CoaAhBotCommands();
}
