/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA Economy: auctions run on game time, not real time (AiPlayerbot.CoaEconomy.AuctionClock.Enable, off by default).
// The core ends an auction at a real date, so a server off for a night found every auction over at start-up.
// The world writes a sign of life every minute; at start-up, before the first world update, the time the server was
// off is added to the end of every auction, in memory and in the characters database.

#include "CoaEconomyDb.h"

#include "CoaEconomyConf.h"
#include "CoaEconomyData.h"
#include "AuctionHouseMgr.h"
#include "CoaEconomyRules.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Log.h"
#include "Playerbots.h"
#include "ScriptMgr.h"

namespace CoaEconomy::AuctionClock
{

namespace
{
    constexpr char const* TableName = "coa_economy_clock";
    constexpr uint32 BeatMs = 60 * IN_MILLISECONDS;
    uint32 sinceBeat = 0;
    bool tableExists = false;

    uint32 Now()
    {
        return uint32(GameTime::GetGameTime().count());
    }

    void Beat()
    {
        if (tableExists)
            PlayerbotsDatabase.Execute("REPLACE INTO {} (name, value) VALUES ('last_alive', {})", TableName, Now());
    }

    void Resume()
    {
        tableExists = Db::HasTable(TableName, {"name", "value"});
        if (!tableExists)
        {
            LOG_ERROR("server.loading", "CoA Economy: table {} is missing or out of date in the playerbots database; auctions run on real "
                "time", TableName);
            return;
        }

        uint32 lastAlive = 0;
        if (QueryResult result = PlayerbotsDatabase.Query("SELECT value FROM {} WHERE name = 'last_alive'", TableName))
            lastAlive = result->Fetch()[0].Get<uint32>();

        uint32 const maxDays = CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.AuctionClock.MaxPauseDays", 30);
        uint32 const pause = CoaEconomyRules::AuctionPause(lastAlive, Now(), 5 * MINUTE, maxDays * DAY);
        uint32 moved = 0;
        if (pause)
        {
            for (AuctionHouseId house : {AuctionHouseId::Alliance, AuctionHouseId::Horde, AuctionHouseId::Neutral})
                if (AuctionHouseObject* auctions = sAuctionMgr->GetAuctionsMapByHouseId(house))
                    for (auto const& [id, auction] : auctions->GetAuctions())
                        if (auction)
                        {
                            auction->expire_time += pause;
                            ++moved;
                        }
            CharacterDatabase.DirectExecute("UPDATE auctionhouse SET time = time + {}", pause);
            LOG_INFO("server.loading", "CoA Economy: the server was off {} min; {} auctions resume where they stopped",
                     pause / MINUTE, moved);
        }
        Data::RecordClock(pause, moved);
        Beat();
    }

    class CoaAuctionClockWorld : public WorldScript
    {
    public:
        CoaAuctionClockWorld() : WorldScript("CoaAuctionClockWorld", {WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE,
                                                                     WORLDHOOK_ON_SHUTDOWN}) { }

        // Called once the auctions are loaded and before the first world update.
        void OnStartup() override { Resume(); }

        void OnUpdate(uint32 diff) override
        {
            sinceBeat += diff;
            if (sinceBeat < BeatMs)
                return;
            sinceBeat = 0;
            Beat();
        }

        void OnShutdown() override { Beat(); }
    };
}

} // namespace CoaEconomy::AuctionClock

void AddSC_coa_economy_auction_clock()
{
    // Off by default; read at start-up only (restart to change it).
    if (!CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.AuctionClock.Enable", true, false))
        return;
    new CoaEconomy::AuctionClock::CoaAuctionClockWorld();
}
