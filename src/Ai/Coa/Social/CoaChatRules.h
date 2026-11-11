#ifndef _PLAYERBOT_COACHATRULES_H
#define _PLAYERBOT_COACHATRULES_H

// AiPlayerbot.CoaLive.Chat.Enable (the launcher's "Bot chat" switch): what of a CoA Live line is played. Off, the
// bots write nothing (channels, guild, night bank, whisper answers, Trade), but what goes with a line still happens:
// its emote, the group invite of a "sure, inviting", the COD mail of a sale. Pure rules (tests).
namespace CoaChat
{
struct Parts
{
    bool text = true;
    bool emote = true;
    bool invite = true;
    bool cod = true;
};

inline Parts PartsOf(bool chatOn)
{
    Parts p;
    p.text = chatOn;
    return p;
}

// The Trade pace only picks speakers for lines: off, it has nothing to do.
inline bool TradePaceRuns(bool chatOn, unsigned perMinute) { return chatOn && perMinute; }

// The module's older lines (broadcasts, suggestions, RandomBotTalk) go out by the guild, world and zone channels
// (PlayerbotAI::SayTo*): off, none of them. A bot's reply to what a player says near it or in a channel is
// kept for its master only (an order and its answer are not chat).
inline bool ChannelLineAllowed(bool chatOn) { return chatOn; }
inline bool ChatReplyAllowed(bool chatOn, bool fromMaster) { return chatOn || fromMaster; }
}  // namespace CoaChat

#endif
