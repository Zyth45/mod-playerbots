/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_ECONOMY_ARTISANS_ADDON_RULES_H
#define COA_ECONOMY_ARTISANS_ADDON_RULES_H

#include <cstdint>
#include <string>
#include <vector>

// CoA Economy: the pure rules of the artisans' addon protocol (WORLD-PROTOCOLE-3.0.md §0 and §2), no game types.
namespace CoaArtisansAddonRules
{
    constexpr char const* Prefix = "SQBW\t";
    constexpr std::size_t MaxLine = 254;          // an addon message, prefix included
    constexpr uint32_t QuoteSeconds = 10 * 60;    // a quote holds 10 minutes
    constexpr uint32_t MinRequestMs = 1000;       // 1 request per second per player
    constexpr uint32_t CacheMs = 10 * 1000;       // list answers cached 10 s per player
    constexpr uint32_t MaxQty = 5;                // one CoD mail
    constexpr uint32_t PriceRisePercent = 10;     // AORDER refuses a price risen more than this
    constexpr std::size_t MaxArtisans = 40;
    constexpr std::size_t MaxRecipes = 60;
    constexpr std::size_t MaxMails = 20;
    constexpr uint32_t MailDays = 7;

    inline std::vector<std::string> SplitTabs(std::string const& text)
    {
        std::vector<std::string> fields;
        std::size_t start = 0;
        while (true)
        {
            std::size_t const tab = text.find('\t', start);
            fields.push_back(text.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
            if (tab == std::string::npos)
                return fields;
            start = tab + 1;
        }
    }

    // A whole positive number, nothing else ("3"; not "3x", "", "-1"); 0 when it is none.
    inline uint32_t ParseNumber(std::string const& text)
    {
        if (text.empty() || text.size() > 9)
            return 0;
        uint32_t value = 0;
        for (char c : text)
        {
            if (c < '0' || c > '9')
                return 0;
            value = value * 10 + uint32_t(c - '0');
        }
        return value;
    }

    inline bool QtyOk(uint32_t qty) { return qty >= 1 && qty <= MaxQty; }

    inline bool QuoteExpired(uint32_t quotedAt, uint32_t now) { return now > quotedAt + QuoteSeconds; }

    // The price now is more than PriceRisePercent over the quoted one.
    inline bool PriceRose(uint64_t quoted, uint64_t now)
    {
        return now * 100 > quoted * (100 + PriceRisePercent);
    }

    // A new request is taken when the last one is at least a second old (first request: always).
    inline bool RateAllowed(uint64_t lastMs, uint64_t nowMs, bool first)
    {
        return first || nowMs >= lastMs + MinRequestMs;
    }

    inline bool CacheFresh(uint64_t builtMs, uint64_t nowMs) { return nowMs < builtMs + CacheMs; }

    // The line on the wire: the prefix and the fields, cut (never split) to fit an addon message.
    inline std::string Wire(std::string const& line)
    {
        std::string text = std::string(Prefix) + line;
        if (text.size() > MaxLine)
            text.resize(MaxLine);
        return text;
    }

    // A field may hold no tab or line break (a name, a reason).
    inline std::string Clean(std::string text)
    {
        for (char& c : text)
            if (c == '\t' || c == '\n' || c == '\r')
                c = ' ';
        return text;
    }

    // ---- Order tracking (AMAIL): preparing -> sent -> awaiting -> paid / returned / cancelled ----
    constexpr uint32_t SendDelayMs = 5000;  // the artisan "crafts" 5 s before the mail goes (its whispers: 2-9 s)
    constexpr uint32_t ReturnGraceSeconds = 10; // a sent mail not yet seen in the mailbox is not "returned" before this

    inline bool IsFinal(std::string const& state)
    {
        return state == "paid" || state == "returned" || state == "cancelled";
    }

    // The state of a sent order, from what the player's mailbox shows (the player is online).
    // paid: the payment mail came back; inMailbox: the CoD mail is there; delivered: its delivery time is past.
    inline std::string NextState(std::string const& state, bool paid, bool inMailbox, bool delivered, uint32_t sinceSent)
    {
        if (IsFinal(state) || state == "preparing")
            return state;
        if (paid)
            return "paid";
        if (inMailbox)
            return delivered ? "awaiting" : "sent";
        return sinceSent < ReturnGraceSeconds ? state : "returned";
    }
}

#endif
