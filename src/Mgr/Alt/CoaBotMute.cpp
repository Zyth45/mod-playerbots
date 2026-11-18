/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaBotMute.h"

#include "CoaAltAddon.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include <atomic>
#include <mutex>
#include <unordered_map>

namespace
{
// Bot -> the master who muted it. Read by every chatting bot: a count spares the lock while nobody is muted.
std::mutex muteLock;
std::unordered_map<ObjectGuid::LowType, ObjectGuid::LowType> mutedBy;
std::atomic<uint32> mutedCount{0};

void Set(Player* bot, Player* master, bool on)
{
    std::lock_guard<std::mutex> guard(muteLock);
    if (on)
        mutedBy[bot->GetGUID().GetCounter()] = master->GetGUID().GetCounter();
    else
        mutedBy.erase(bot->GetGUID().GetCounter());
    mutedCount.store(uint32(mutedBy.size()), std::memory_order_relaxed);
}
}  // namespace

bool CoaBotMute::IsMuted(Player* bot)
{
    if (!bot || !mutedCount.load(std::memory_order_relaxed) || !sPlayerbotAIConfig.coaMute)
        return false;
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    Player* master = botAI ? botAI->GetMaster() : nullptr;
    if (!master)
        return false;
    std::lock_guard<std::mutex> guard(muteLock);
    auto const it = mutedBy.find(bot->GetGUID().GetCounter());
    return it != mutedBy.end() && it->second == master->GetGUID().GetCounter();
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
        Set(bot, master, arg == "on");
    CoaAltAddon::SendToMaster(botAI, std::string("MUTE\t") + (IsMuted(bot) ? "1" : "0"));
    return true;
}
