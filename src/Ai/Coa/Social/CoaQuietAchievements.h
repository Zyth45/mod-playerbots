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

// CoA 3.2 (AiPlayerbot.Coa.HideLoginAchievementsMinutes = 3, the user's go of 21/11): a random bot that comes online
// earns a run of achievements in its first minutes (Level 10 ... 60 when it is levelled at login): they stay earned,
// they are not announced to its guild, the players around it or the realm until that many minutes after it was
// loaded; then its achievements are announced as before. Never a real player, never a player's alt bot.
inline bool InLoginWindow(uint64_t nowMs, uint64_t untilMs) { return nowMs < untilMs; }

// CoA 3.2 (AiPlayerbot.Coa.AchievementAnnounceGapSeconds = 60): a random bot online already announced "An Honorable
// Kill", "100", "500" and "1000 Honorable Kills" to its guild within seconds. At most one announcement per bot in
// that gap: the others are earned without a word. lastMs 0: none announced yet.
inline bool GapMutes(uint64_t nowMs, uint64_t lastMs, uint64_t gapMs)
{
    return gapMs && lastMs && nowMs < lastMs + gapMs;
}

// CoA 3.2 (AiPlayerbot.Coa.AchievementAnnounceRealmGapSeconds = 60, the user's ask of 22/11: when the realm starts,
// every bot levels up and announces its Level achievements at once). On top of the gap per bot, at most one
// announcement of a random bot in the whole realm per realm gap (a Server First counted too). Announced: botLastMs
// and realmLastMs both become nowMs; muted: neither moves.
inline bool Announces(uint64_t nowMs, uint64_t botLastMs, uint64_t realmLastMs, uint64_t botGapMs,
                      uint64_t realmGapMs)
{
    return !GapMutes(nowMs, botLastMs, botGapMs) && !GapMutes(nowMs, realmLastMs, realmGapMs);
}

// CoA 3.2.1 (AiPlayerbot.Coa.HideReplayedAchievements = 1): the core never saves a bot's achievements (both cores,
// AchievementMgr::SaveToDB returns for a bot session), so after each restart a bot earns them again: at its first
// level-up, "Level 10" ... up to its level all at once. A level achievement below the bot's level is such a replay:
// earned without a word. requiredLevel 0: not a level achievement.
inline bool LevelReplay(uint32_t botLevel, uint32_t requiredLevel)
{
    return requiredLevel && requiredLevel < botLevel;
}

// CoA 3.2.1 (AiPlayerbot.Coa.AchievementAnnounceLog): what was decided for the achievements of the random bots, an hour
// at a time. A candidate is shown or muted for one reason.
enum class AnnounceFate { Shown, Simulated, Login, Replay, BotGap, RealmGap, Count };

inline AnnounceFate GapFate(uint64_t nowMs, uint64_t botLastMs, uint64_t realmLastMs, uint64_t botGapMs,
                            uint64_t realmGapMs)
{
    if (GapMutes(nowMs, botLastMs, botGapMs))
        return AnnounceFate::BotGap;
    if (GapMutes(nowMs, realmLastMs, realmGapMs))
        return AnnounceFate::RealmGap;
    return AnnounceFate::Shown;
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
