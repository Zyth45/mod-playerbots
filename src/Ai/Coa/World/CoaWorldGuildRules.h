/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_WORLD_GUILD_RULES_H
#define COA_WORLD_GUILD_RULES_H

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>

// The rules of the guild orders and the guild's gear gifts (CoaWorldGuilds), apart from the game objects so that
// tests/coa_world_guild_rules.cpp can check them.

namespace CoaWorldGuildRules
{
    // A guild chat line that asks the guild for an item: an item link and a word of asking ("anyone have [x]?",
    // "need [x]", "can someone craft [x]", "lf [x]"). Returns the item entry, 0 when the line asks for nothing.
    inline std::uint32_t OrderedItem(std::string const& message)
    {
        std::size_t const link = message.find("|Hitem:");
        if (link == std::string::npos)
            return 0;
        std::uint32_t const item = std::uint32_t(std::strtoul(message.c_str() + link + 7, nullptr, 10));
        if (!item)
            return 0;

        // The words outside the link, lower case.
        std::string text = message.substr(0, link);
        std::size_t const end = message.find("|h|r", link);
        if (end != std::string::npos)
            text += " " + message.substr(end + 4);
        // Lower case, punctuation as spaces ("anyone?" is "anyone").
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char c) { return std::isalnum(c) ? char(std::tolower(c)) : ' '; });
        text = " " + text + " ";

        static char const* const asking[] = { " need ", " anyone ", " anybody ", " someone ", " somebody ", " craft ",
                                              " make ", " lf ", " looking for ", " spare ", " can i get ", " wtb ",
                                              " got a ", " has a ", " have a ", " have an " };
        for (char const* word : asking)
            if (text.find(word) != std::string::npos)
                return item;
        return 0;
    }

    // What the materials cost a guildmate who crafts for you: each reagent at its vendor price, or four times what a
    // vendor gives for it when no vendor sells it (cloth, ore, herbs). No fee between guildmates.
    inline std::uint64_t ReagentPrice(std::uint32_t buyPrice, std::uint32_t sellPrice)
    {
        return buyPrice ? buyPrice : std::uint64_t(sellPrice) * 4;
    }

    // Orders a player may place a day.
    inline bool MayOrder(std::uint32_t placedToday, std::uint32_t perDay)
    {
        return placedToday < perDay;
    }

    // A gift of gear is an upgrade worth sending: clearly better than what is worn (15 % and a little more), or a
    // slot with nothing in it.
    inline bool WorthGiving(float newScore, float wornScore)
    {
        return newScore > 0.0f && newScore > wornScore * 1.15f + 1.0f;
    }

    // One gift every `everyDays` days at most (day numbers since the epoch; 0 = never given).
    inline bool GiftDue(std::uint32_t today, std::uint32_t lastDay, std::uint32_t everyDays)
    {
        return !lastDay || today >= lastDay + everyDays;
    }

    // The required levels a gift may have: the player's level and the four below it.
    inline bool GiftLevelFits(std::uint32_t requiredLevel, std::uint32_t playerLevel)
    {
        return requiredLevel <= playerLevel && requiredLevel + 4 >= playerLevel;
    }

    // A hello in guild chat: a short line (4 words at most) with a greeting word ("hi", "hey", "bonjour", "re"...).
    inline bool IsGreeting(std::string text)
    {
        if (text.find("o/") != std::string::npos || text.find("\\o") != std::string::npos)
            return text.size() <= 12;
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        for (char& c : text)
            if (!std::isalnum(static_cast<unsigned char>(c)))
                c = ' ';
        static char const* const words[] = { "hello", "hi", "hey", "heya", "hiya", "yo", "sup", "re", "hola", "hallo",
                                             "bonjour", "salut", "cc", "coucou", "slt", "bjr", "bonsoir", "yop", "plop" };
        size_t count = 0;
        bool greeting = false;
        size_t i = 0;
        while (i < text.size())
        {
            while (i < text.size() && text[i] == ' ')
                ++i;
            size_t const start = i;
            while (i < text.size() && text[i] != ' ')
                ++i;
            if (i == start)
                break;
            ++count;
            std::string const word = text.substr(start, i - start);
            for (char const* w : words)
                if (word == w)
                    greeting = true;
        }
        return greeting && count <= 4;
    }
}

#endif
