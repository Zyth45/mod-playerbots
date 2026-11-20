/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaBotMute.h"

#include "CoaAltAddon.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "WorldSession.h"
#include <atomic>
#include <mutex>
#include <unordered_set>

namespace
{
// The masters who muted their bots. Read by every chatting bot: a count spares the lock while nobody did.
std::mutex muteLock;
std::unordered_set<ObjectGuid::LowType> mutedMasters;
std::atomic<uint32> mutedCount{0};

bool MasterMuted(ObjectGuid::LowType master)
{
    std::lock_guard<std::mutex> guard(muteLock);
    return mutedMasters.count(master) != 0;
}

void Set(ObjectGuid::LowType master, bool on)
{
    {
        std::lock_guard<std::mutex> guard(muteLock);
        bool const changed = on ? mutedMasters.insert(master).second : mutedMasters.erase(master) > 0;
        mutedCount.store(uint32(mutedMasters.size()), std::memory_order_relaxed);
        if (!changed)
            return;
    }
    if (on)
        PlayerbotsDatabase.Execute("REPLACE INTO `playerbots_coa_mute` (`player`) VALUES ({})", master);
    else
        PlayerbotsDatabase.Execute("DELETE FROM `playerbots_coa_mute` WHERE `player` = {}", master);
}
}  // namespace

void CoaBotMute::Load()
{
    std::lock_guard<std::mutex> guard(muteLock);
    mutedMasters.clear();
    if (QueryResult result = PlayerbotsDatabase.Query("SELECT `player` FROM `playerbots_coa_mute`"))
    {
        do
            mutedMasters.insert(result->Fetch()[0].Get<uint32>());
        while (result->NextRow());
    }
    mutedCount.store(uint32(mutedMasters.size()), std::memory_order_relaxed);
}

bool CoaBotMute::IsMuted(Player* bot)
{
    if (!bot || !mutedCount.load(std::memory_order_relaxed) || !sPlayerbotAIConfig.coaMute)
        return false;
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    Player* master = botAI ? botAI->GetMaster() : nullptr;
    if (!master || master == bot || !master->GetSession() || master->GetSession()->IsBot())
        return false;
    return MasterMuted(master->GetGUID().GetCounter());
}

bool CoaBotMute::Command(PlayerbotAI* botAI, std::string const& command, std::string const& arg)
{
    if (command != "mute")
        return false;
    Player* bot = botAI ? botAI->GetBot() : nullptr;
    Player* master = botAI ? botAI->GetMaster() : nullptr;
    if (!bot || !master)
        return true;
    if (sPlayerbotAIConfig.coaMute && (arg == "on" || arg == "off"))
        Set(master->GetGUID().GetCounter(), arg == "on");
    CoaAltAddon::SendToMaster(botAI, std::string("MUTE\t") + (IsMuted(bot) ? "1" : "0"));
    return true;
}
