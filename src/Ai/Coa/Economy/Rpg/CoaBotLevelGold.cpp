/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA Economy: a random bot's gold on reaching some levels (AiPlayerbot.CoaEconomy.BotLevelGold, by default
// 0.5 gold at 10 up to 50 gold at 80). With BotStartGoldPercent = 0 (the default), bots start with nothing and get a little at these levels,
// as a player gains from quests and loot. Paid on a real level-up only (one level at a time), never to a bot set to a
// level at once, so a realm's start pays nobody in bulk.

#include "CoaEconomyConf.h"
#include "CoaEconomyRules.h"
#include "Player.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"

#include <map>
#include <mutex>
#include <string>

namespace
{
    std::map<uint32, uint32> levelGold;
    std::mutex levelGoldLock;

    void LoadLevelGold()
    {
        static char const* const defaultLevelGold = "10:5000,15:10000,20:20000,25:30000,30:50000,35:70000,"
                                                    "40:100000,45:120000,50:150000,55:200000,60:250000,65:300000,"
                                                    "70:350000,75:400000,80:500000";
        std::map<uint32, uint32> table = CoaEconomyRules::ParseLevelGold(
            CoaEconomy::Conf::Get<std::string>("AiPlayerbot.CoaEconomy.BotLevelGold", defaultLevelGold, false));
        std::lock_guard<std::mutex> lock(levelGoldLock);
        levelGold = std::move(table);
    }

    class CoaBotLevelGoldWorld : public WorldScript
    {
    public:
        CoaBotLevelGoldWorld() : WorldScript("CoaBotLevelGoldWorld", {WORLDHOOK_ON_AFTER_CONFIG_LOAD}) { }
        void OnAfterConfigLoad(bool /*reload*/) override { LoadLevelGold(); }
    };

    class CoaBotLevelGoldPlayer : public PlayerScript
    {
    public:
        CoaBotLevelGoldPlayer() : PlayerScript("CoaBotLevelGoldPlayer", {PLAYERHOOK_ON_LEVEL_CHANGED}) { }

        void OnPlayerLevelChanged(Player* player, uint8 oldLevel) override
        {
            if (!player || !sRandomPlayerbotMgr.IsRandomBot(player))
                return;
            uint32 copper = 0;
            {
                std::lock_guard<std::mutex> lock(levelGoldLock);
                if (levelGold.empty())
                    return;
                copper = CoaEconomyRules::LevelGoldFor(levelGold, oldLevel, player->GetLevel());
            }
            if (copper && player->GetMoney() + copper <= MAX_MONEY_AMOUNT)
                player->ModifyMoney(int32(copper));
        }
    };
}

void AddSC_coa_economy_bot_level_gold()
{
    LoadLevelGold();
    new CoaBotLevelGoldWorld();
    new CoaBotLevelGoldPlayer();
}
