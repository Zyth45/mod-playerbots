/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COAQUICKASKRULES_H
#define _PLAYERBOT_COAQUICKASKRULES_H

// The quick asks of a real player to the bots of its group (3.1.3, SquidBots Lite /sbl keys bar):
// - "heal please": the healers of the group bring the player's friendly target (else the player) back to full health,
//   cast after cast (AiPlayerbot.CoaLive.HealRequest.Enable);
// - "cc please": ONE bot of the group with a crowd control ready, valid on that creature and in range, casts it on the
//   player's hostile target; the moon marks it and the other bots leave it (AiPlayerbot.CoaLive.CcRequest.Enable);
// - "taunt please": the tank of the group taunts the player's hostile target and keeps it; with no taunt ready it
//   takes it by threat and says so (AiPlayerbot.CoaLive.TauntRequest.Enable).
// Heard in a whisper to a bot, party, raid or /say chat, or from the addon (SQBA "qa <heal|cc|taunt> [<guid hex>]",
// answered "QA\tdone\t<action>\t<bot>\t<spellId>\t<cdLeftMs>" or "QA\tnone\t<action>\t<reason>"; "qastate" answered
// "QA\tst\t<action>\t<readyBots>\t<nextReadyMs>" per action, then "QA\t-"). Without the game objects, for the test.

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "CoaHealRequestRules.h"

namespace CoaQuickAsk
{
enum class Kind : uint8_t
{
    None,
    Heal,
    Cc,
    Taunt
};

// Where the ask was heard: the answer goes back there.
enum class Origin : uint8_t
{
    Addon,
    Whisper,
    Party,
    Raid,
    Say
};

constexpr uint64_t PLAYER_GAP_MS = 2000;    // one ask of a kind per player in this time (a double click, a spammed key)
constexpr uint64_t ANSWER_MS = 1500;        // a cc or taunt nobody could cast in this time is answered "none"
constexpr uint64_t HEAL_WINDOW_MS = 10000;  // the healers keep healing the target this long at most
constexpr uint64_t HEAL_FALLBACK_MS = 1000; // a bot of another role with a heal helps when no healer cast by then
constexpr uint32_t HEAL_MAX_CD_MS = 60000;  // a heal with a longer cooldown (Lay on Hands...) is kept for the healer
constexpr uint32_t CC_MIN_DURATION_MS = 4000;  // a shorter stun is an interrupt, not a crowd control
constexpr uint32_t BIG_CD_MS = 60000;       // a defensive of the key spells bar: a cooldown of a minute or more
constexpr uint32_t MAX_WORDS = 4;           // "cc please" and "taunt please" are short lines

inline char const* Name(Kind kind)
{
    switch (kind)
    {
        case Kind::Heal:
            return "heal";
        case Kind::Cc:
            return "cc";
        case Kind::Taunt:
            return "taunt";
        default:
            return "";
    }
}

inline Kind KindOf(std::string const& name)
{
    if (name == "heal")
        return Kind::Heal;
    if (name == "cc")
        return Kind::Cc;
    if (name == "taunt")
        return Kind::Taunt;
    return Kind::None;
}

// The reasons of "QA none", the strongest last: a bot that could have done it (on cooldown, out of range, busy) says
// more than "nobody has one".
enum class Reason : uint8_t
{
    None,
    NoBot,     // noheal, nocc, notank: no bot of the group has the spell
    Immune,    // the target is a boss, already under a crowd control, or immune to the spell
    Cd,        // a bot has it, on cooldown
    Range,     // a bot has it, ready, out of range or sight
    Busy,      // a bot has it, ready and in range, and could not cast it
    Held,      // taunt: the tank already holds the target
    Full,      // heal: the target is at full health
    NoTarget,  // no valid target
    NotGroup,  // the target is not of the player's group (heal) or the player has no group
    Off        // the option is off
};

inline char const* ReasonName(Kind kind, Reason reason)
{
    switch (reason)
    {
        case Reason::NoBot:
            return kind == Kind::Heal ? "noheal" : kind == Kind::Cc ? "nocc" : "notank";
        case Reason::Immune:
            return "immune";
        case Reason::Cd:
            return "cd";
        case Reason::Range:
            return "range";
        case Reason::Busy:
            return "busy";
        case Reason::Held:
            return "held";
        case Reason::Full:
            return "full";
        case Reason::NoTarget:
            return "notarget";
        case Reason::NotGroup:
            return "notgroup";
        case Reason::Off:
            return "off";
        default:
            return "noheal";
    }
}

// The reason kept for the answer: the stronger of the two (the order of Reason up to Busy).
inline Reason Stronger(Reason a, Reason b) { return uint8_t(b) > uint8_t(a) ? b : a; }

inline std::string DoneLine(Kind kind, std::string const& bot, uint32_t spellId, uint32_t cdLeftMs)
{
    return std::string("QA\tdone\t") + Name(kind) + "\t" + bot + "\t" + std::to_string(spellId) + "\t" +
           std::to_string(cdLeftMs);
}

inline std::string NoneLine(Kind kind, Reason reason)
{
    return std::string("QA\tnone\t") + Name(kind) + "\t" + ReasonName(kind, reason);
}

inline std::string StateLine(Kind kind, uint32_t readyBots, uint32_t nextReadyMs)
{
    return std::string("QA\tst\t") + Name(kind) + "\t" + std::to_string(readyBots) + "\t" + std::to_string(nextReadyMs);
}

inline bool IsPleaseWord(std::string const& w)
{
    return w == "please" || w == "plz" || w == "pls" || w == "plox" || w == "stp" || w == "svp" || w == "now";
}

inline bool IsTargetWord(std::string const& w)
{
    return w == "this" || w == "that" || w == "it" || w == "target" || w == "him" || w == "her" || w == "them";
}

// A line that is only an ask: "<word>", "<word> please", "<word> it", "<word> this please". The word alone counts
// for heal and taunt, never for "cc" (in French it is "coucou", a hello to the group, 15/11).
inline Kind ParseLine(std::string const& text)
{
    std::vector<std::string> const words = CoaHealRequest::Words(text);
    if (words.empty() || words.size() > MAX_WORDS)
        return Kind::None;
    std::string const& head = words[0];
    Kind kind = Kind::None;
    if (head == "cc")
        kind = Kind::Cc;
    else if (head == "taunt" || head == "taunts")
        kind = Kind::Taunt;
    else if (CoaHealRequest::IsHealWord(head) || CoaHealRequest::IsSoinWord(head))
        kind = Kind::Heal;
    if (kind == Kind::None)
        // "heal" as part of a longer ask ("need heal", "besoin de soin", "soigne-moi").
        return words.size() <= CoaHealRequest::MAX_WORDS && CoaHealRequest::IsHealRequest(text) ? Kind::Heal : Kind::None;
    if (words.size() == 1)
        return kind == Kind::Cc ? Kind::None : kind;
    // Every other word asks: please, me (heal), it/this/target (cc, taunt). "taunt the boss", "cc broke" are talk.
    bool please = false;
    for (size_t i = 1; i < words.size(); ++i)
    {
        std::string const& w = words[i];
        if (IsPleaseWord(w) || CoaHealRequest::IsPlease(w))
            please = true;
        else if (kind != Kind::Heal && IsTargetWord(w))
            continue;
        else
            return Kind::None;
    }
    // "cc this" asks too; "cc" needs a word after it, which it has here.
    return please || kind != Kind::Heal ? kind : Kind::None;
}

// "qa <heal|cc|taunt> [<guid hex>]" of the addon. false: not understood.
inline bool ParseAddon(std::string const& arg, Kind& kind, uint64_t& guid)
{
    std::string const name = arg.substr(0, arg.find(' '));
    kind = KindOf(name);
    guid = 0;
    if (kind == Kind::None)
        return false;
    if (name.size() >= arg.size())
        return true;
    std::string hex = arg.substr(name.size() + 1);
    while (!hex.empty() && hex.back() == ' ')
        hex.pop_back();
    if (hex.rfind("0x", 0) == 0 || hex.rfind("0X", 0) == 0)
        hex = hex.substr(2);
    if (hex.empty())
        return true;
    if (hex.size() > 16 || hex.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        return false;
    guid = std::strtoull(hex.c_str(), nullptr, 16);
    return true;
}

// A crowd control spell: a single enemy target, not an area, a stun, fear, confuse (polymorph, sap...) or
// banish lasting CC_MIN_DURATION_MS or more.
inline bool IsCrowdControl(bool passive, bool area, bool enemySingle, bool ccAura, int32_t durationMs)
{
    return !passive && !area && enemySingle && ccAura && (durationMs < 0 || uint32_t(durationMs) >= CC_MIN_DURATION_MS);
}

// The crowd control a bot keeps for "cc please": the longest one, then the shortest cooldown, then the lowest id.
struct CcSpell
{
    uint32_t id = 0;
    int32_t durationMs = 0;
    uint32_t cdMs = 0;
};

inline bool BetterCc(CcSpell const& a, CcSpell const& b)
{
    int64_t const da = a.durationMs < 0 ? INT32_MAX : a.durationMs;
    int64_t const db = b.durationMs < 0 ? INT32_MAX : b.durationMs;
    if (da != db)
        return da > db;
    if (a.cdMs != b.cdMs)
        return a.cdMs < b.cdMs;
    return a.id < b.id;
}

// The category of a spell on the key spells bar (KSPELL 7th field): "def" a big defensive (a cooldown of a minute
// or more, on the caster or a friend: damage taken down, an immunity, an absorb, more health), "burst" a group burst
// (Bloodlust, Heroism and the bursts of the CoA classes that leave a Sated-like debuff), "cc" the bot's crowd control
// (one per bot), "-" anything else.
inline char const* Category(bool burst, bool defensive, uint32_t cdTotalMs, bool isBotCc)
{
    if (burst)
        return "burst";
    if (defensive && cdTotalMs >= BIG_CD_MS)
        return "def";
    if (isBotCc)
        return "cc";
    return "-";
}

// The ask is still to be answered: a cc or taunt within ANSWER_MS, a heal within HEAL_WINDOW_MS.
inline bool Open(Kind kind, uint64_t heardMs, uint64_t nowMs)
{
    return nowMs < heardMs + (kind == Kind::Heal ? HEAL_WINDOW_MS : ANSWER_MS);
}

// A heal ask with no cast yet is answered "none" after ANSWER_MS (the window stays open for a late healer).
inline bool HealSilent(uint64_t heardMs, uint64_t nowMs) { return nowMs >= heardMs + ANSWER_MS; }

// Who may heal now: a healer always, another bot with a heal once HEAL_FALLBACK_MS passed with no cast.
inline bool MayHeal(bool healer, bool anyCast, uint64_t heardMs, uint64_t nowMs)
{
    return healer || (!anyCast && nowMs >= heardMs + HEAL_FALLBACK_MS);
}

// A new ask of the same kind of the same player within PLAYER_GAP_MS is the same ask.
inline bool MayAsk(uint64_t lastMs, uint64_t nowMs) { return !lastMs || nowMs >= lastMs + PLAYER_GAP_MS; }
}  // namespace CoaQuickAsk

#endif
