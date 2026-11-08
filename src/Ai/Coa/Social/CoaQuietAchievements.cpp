/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaQuietAchievements.h"

#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WorldPacket.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace
{
std::shared_mutex quietLock;
std::unordered_map<uint64_t, uint32_t> quiet;  // bot guid -> nested scopes (map threads)
std::atomic<uint32_t> quietCount{0};           // the fast way out of every packet sent

bool IsQuiet(uint64_t guid)
{
    std::shared_lock<std::shared_mutex> guard(quietLock);
    return quiet.count(guid) != 0;
}

class CoaQuietAchievementsServerScript : public ServerScript
{
public:
    CoaQuietAchievementsServerScript() : ServerScript("CoaQuietAchievementsServerScript", {SERVERHOOK_CAN_PACKET_SEND}) {}

    // Every packet of the realm passes here: out at once unless a bot is being made right now.
    bool CanPacketSend(WorldSession* /*session*/, WorldPacket const& packet) override
    {
        if (!quietCount.load(std::memory_order_relaxed))
            return true;
        uint16 const opcode = packet.GetOpcode();
        if (opcode != SMSG_MESSAGECHAT && opcode != SMSG_SERVER_FIRST_ACHIEVEMENT)
            return true;
        uint64_t sender = 0;
        bool const announce =
            opcode == SMSG_MESSAGECHAT
                ? CoaQuietAchievements::AchievementChatSender(packet.contents(), packet.size(), sender)
                : CoaQuietAchievements::ServerFirstSender(packet.contents(), packet.size(), sender);
        return !(announce && IsQuiet(sender));
    }
};
}  // namespace

namespace CoaQuietAchievements
{
Scope::Scope(Player* bot)
{
    if (!bot)
        return;
    guid = bot->GetGUID().GetRawValue();
    std::unique_lock<std::shared_mutex> guard(quietLock);
    if (quiet[guid]++ == 0)
        ++quietCount;
}

Scope::~Scope()
{
    if (!guid)
        return;
    std::unique_lock<std::shared_mutex> guard(quietLock);
    auto it = quiet.find(guid);
    if (it != quiet.end() && --it->second == 0)
    {
        quiet.erase(it);
        --quietCount;
    }
}
}  // namespace CoaQuietAchievements

void AddSC_coa_quiet_achievements() { new CoaQuietAchievementsServerScript(); }
