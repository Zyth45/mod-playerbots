/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA Economy: at the first start in 3.0, the old auction house module's stock goes (the user's decision, 09/11): every
// auction of its seller characters (AuctionHouseBot.GUIDs, and the characters of the AHBOT account) is removed, as
// ".ahbot empty" does; a bidder gets its money back by mail, the item is destroyed (the bot made it from nothing).
// Players' and alts' auctions are never touched. Once only: the row 'ahbot_old_emptied' of coa_economy_clock; a new
// install writes it with nothing to empty. Runs whether SquidBots-AH is on or off (it then posts its own stock, or
// nothing). Off unless AiPlayerbot.CoaEconomy.AHBot.EmptyOldAuctions = 1 (the 3.0 update sets it).

#include "AccountMgr.h"
#include "AuctionHouseMgr.h"
#include "CoaEconomyDb.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "ScriptMgr.h"
#include "StringConvert.h"
#include "Tokenize.h"

#include <unordered_set>
#include <vector>

namespace
{
    constexpr char const* MarkerTable = "coa_economy_clock";
    constexpr char const* MarkerName = "ahbot_old_emptied";
    constexpr char const* OldAccountName = "AHBOT";

    // The old module's sellers: the GUIDs of its settings, and every character of its AHBOT account.
    std::unordered_set<uint32> OldSellers()
    {
        std::unordered_set<uint32> sellers;
        std::string const guids = sConfigMgr->GetOption<std::string>("AuctionHouseBot.GUIDs", "0", false);
        for (std::string_view token : Acore::Tokenize(guids, ',', false))
            if (Optional<uint32> guid = Acore::StringTo<uint32>(token); guid && *guid)
                sellers.insert(*guid);
        if (uint32 account = AccountMgr::GetId(OldAccountName))
            if (QueryResult result = CharacterDatabase.Query("SELECT guid FROM characters WHERE account = {}", account))
                do
                {
                    sellers.insert(result->Fetch()[0].Get<uint32>());
                } while (result->NextRow());
        return sellers;
    }

    void EmptyOldAuctions()
    {
        if (!CoaEconomy::Db::HasTable(MarkerTable, {"name", "value"}))
        {
            LOG_ERROR("server.loading", "CoA Economy: table {} is missing or out of date in the playerbots database; the old "
                      "auction house bot's auctions are left as they are", MarkerTable);
            return;
        }
        if (QueryResult done = PlayerbotsDatabase.Query("SELECT value FROM {} WHERE name = '{}'", MarkerTable, MarkerName))
            return;

        std::unordered_set<uint32> const sellers = OldSellers();
        uint32 removed = 0;
        uint32 refunded = 0;
        if (!sellers.empty())
        {
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            for (AuctionHouseId houseId : {AuctionHouseId::Alliance, AuctionHouseId::Horde, AuctionHouseId::Neutral})
            {
                AuctionHouseObject* house = sAuctionMgr->GetAuctionsMapByHouseId(houseId);
                if (!house)
                    continue;
                std::vector<AuctionEntry*> theirs;
                for (auto const& [id, auction] : house->GetAuctions())
                    if (auction && sellers.count(auction->owner.GetCounter()))
                        theirs.push_back(auction);
                for (AuctionEntry* auction : theirs)
                {
                    if (auction->bidder)
                    {
                        sAuctionMgr->SendAuctionCancelledToBidderMail(auction, trans);
                        ++refunded;
                    }
                    sAuctionMgr->RemoveAItem(auction->item_guid, true, &trans); // the item is deleted
                    auction->DeleteFromDB(trans);
                    house->RemoveAuction(auction);
                    ++removed;
                }
            }
            CharacterDatabase.CommitTransaction(trans);
        }
        PlayerbotsDatabase.DirectExecute("REPLACE INTO {} (name, value) VALUES ('{}', {})", MarkerTable, MarkerName,
                                         uint32(time(nullptr)));
        LOG_INFO("server.loading", "CoA Economy: the old auction house bot's stock is gone: {} auctions of {} seller "
                 "character(s) removed, {} items destroyed, {} bidders refunded by mail; players' auctions untouched",
                 removed, sellers.size(), removed, refunded);
    }

    class CoaAhMigrationWorld : public WorldScript
    {
    public:
        CoaAhMigrationWorld() : WorldScript("CoaAhMigrationWorld", {WORLDHOOK_ON_STARTUP}) { }
        void OnStartup() override { EmptyOldAuctions(); }
    };
}

void AddSC_coa_economy_ah_migration()
{
    if (!sConfigMgr->GetOption<bool>("AiPlayerbot.CoaEconomy.AHBot.EmptyOldAuctions", false, false))
        return;
    new CoaAhMigrationWorld();
}
