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
}  // namespace CoaChat

#endif
