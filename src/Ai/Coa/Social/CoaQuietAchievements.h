/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAQUIETACHIEVEMENTS_H
#define PLAYERBOTS_COAQUIETACHIEVEMENTS_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

class Player;

// A random bot made or levelled up by the factory (PlayerbotFactory::Randomize) earns a run of achievements at once
// (Level 10 ... 50, the professions...), and the core announced each one to the players around it and its guild:
// twenty lines in a player's chat when a bot came online. The achievements stay earned; only their announcement,
// while the factory works on the bot, is not sent (ServerScript::CanPacketSend).
namespace CoaQuietAchievements
{
// While it lives, the bot's achievements are earned without being announced.
class Scope
{
public:
    explicit Scope(Player* bot);
    ~Scope();
    Scope(Scope const&) = delete;
    Scope& operator=(Scope const&) = delete;

private:
    uint64_t guid = 0;
};

// The announcement of an achievement in a SMSG_MESSAGECHAT body (ChatHandler::BuildChatPacket: uint8 type, int32
// language, uint64 sender...): CHAT_MSG_ACHIEVEMENT (0x30) or CHAT_MSG_GUILD_ACHIEVEMENT (0x31), and its sender.
inline bool AchievementChatSender(uint8_t const* body, size_t size, uint64_t& sender)
{
    if (!body || size < 1 + 4 + 8 || (body[0] != 0x30 && body[0] != 0x31))
        return false;
    std::memcpy(&sender, body + 1 + 4, sizeof(sender));  // little endian, as the packet is written
    return true;
}

// A SMSG_SERVER_FIRST_ACHIEVEMENT body: the name (a C string), then the player's guid.
inline bool ServerFirstSender(uint8_t const* body, size_t size, uint64_t& sender)
{
    if (!body)
        return false;
    size_t const end = std::find(body, body + size, uint8_t(0)) - body;
    if (end >= size || size - end - 1 < sizeof(sender))
        return false;
    std::memcpy(&sender, body + end + 1, sizeof(sender));
    return true;
}
}  // namespace CoaQuietAchievements

void AddSC_coa_quiet_achievements();

#endif
