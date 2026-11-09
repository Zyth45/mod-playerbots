/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COASOCIALRULES_H
#define PLAYERBOTS_COASOCIALRULES_H

#include <cstdint>

// CoA Live, L2 social bots (AiPlayerbot.CoaLive.Social.Enable): a free random bot passing a real player of its
// faction raises them, helps them in a fight they are losing, buffs them, or asks them to group when they are
// alone. Pure rules: what to do for one player, and how often (the game side keeps the cooldowns).
namespace CoaSocial
{
enum class Act
{
    None,
    Rez,
    Help,
    Buff,
    Invite,
};

struct PlayerView
{
    bool sameFaction = true;
    bool alive = true;
    bool ghost = false;           // released: a spell can no longer raise the corpse
    bool resurrectAsked = false;  // someone already offered a resurrection
    bool inCombat = false;
    bool inGroup = false;
    uint32_t healthPct = 100;
    uint32_t attackers = 0;
    uint32_t level = 1;
    float distance = 0.0f;
    bool inSettlement = false;    // a city, a town, an inn or a sanctuary: never asked to group there
};

struct BotView
{
    uint32_t level = 1;
    bool inGroup = false;
    bool inInstance = false;
    bool canHeal = false;
};

// A capital and its districts, a town with an inn, a rest area or a sanctuary (AreaTable flags): where a player
// does not want to be asked to group (user, 09/11).
constexpr uint32_t SETTLEMENT_AREA_FLAGS = 0x00000008 /* slave capital */ | 0x00000020 /* slave capital 2 */ |
                                         0x00000100 /* capital */ | 0x00000200 /* city */ |
                                         0x00000800 /* sanctuary */ | 0x00200000 /* town */ |
                                         0x00400000 /* rest zone horde */ | 0x00800000 /* rest zone alliance */;

inline bool IsSettlement(uint32_t zoneFlags, uint32_t areaFlags, bool resting)
{
    return resting || ((zoneFlags | areaFlags) & SETTLEMENT_AREA_FLAGS) != 0;
}

constexpr float ACT_RANGE = 30.0f;
constexpr uint32_t INVITE_LEVEL_GAP = 3;
constexpr uint32_t HELP_LEVEL_GAP = 5;

// A fight the player is losing: low health, or more than one enemy on them.
inline bool InTrouble(PlayerView const& p) { return p.inCombat && (p.healthPct < 50 || p.attackers >= 2); }

// What the bot does for this player, most needed first: raise, help, buff, invite.
inline Act Decide(BotView const& bot, PlayerView const& p)
{
    if (!p.sameFaction || p.distance > ACT_RANGE)
        return Act::None;
    if (!p.alive)
        return !p.ghost && !p.resurrectAsked ? Act::Rez : Act::None;
    if (InTrouble(p))
        // Too far above the bot, its help would not count; a healer always helps (a heal is a heal).
        return bot.canHeal || bot.level + HELP_LEVEL_GAP >= p.level ? Act::Help : Act::None;
    if (p.inCombat)
        return Act::None;
    // Only out in the wild (a forest, the countryside): never in a city, a village, an inn or a sanctuary.
    if (!p.inGroup && !bot.inGroup && !bot.inInstance && !p.inSettlement &&
        (bot.level > p.level ? bot.level - p.level : p.level - bot.level) <= INVITE_LEVEL_GAP)
        return Act::Invite;  // the game side tries a buff first when the invite is on cooldown
    return Act::Buff;
}

// How long before any bot does the same thing for the same player again (seconds), and the chance per look.
// inviteS: AiPlayerbot.CoaLive.Social.InviteCooldownMinutes (one ask an hour per player by default, all bots
// together: user, 09/11).
inline uint32_t PlayerCooldownS(Act a, uint32_t inviteS = 3600)
{
    switch (a)
    {
        case Act::Rez: return 60;
        case Act::Help: return 20;
        case Act::Buff: return 600;
        case Act::Invite: return inviteS;
        default: return 0;
    }
}

inline uint32_t ChancePct(Act a)
{
    switch (a)
    {
        case Act::Rez: return 100;
        case Act::Help: return 100;
        case Act::Buff: return 30;
        case Act::Invite: return 15;
        default: return 0;
    }
}
}  // namespace CoaSocial

#endif
