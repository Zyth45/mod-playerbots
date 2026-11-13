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
#include <ctime>
#include <vector>
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

    // A gift fits the player's role: a caster or healer gets no piece that only has physical stats (strength,
    // agility, attack power), a physical spec none that only has caster stats (intellect, spirit, spell power,
    // mana regeneration). Pieces with neither (stamina, armor, plain weapon damage) fit everyone.
    inline bool RoleFits(bool caster, int physicalStats, int casterStats)
    {
        if (caster)
            return !(physicalStats > 0 && casterStats == 0);
        return !(casterStats > 0 && physicalStats == 0);
    }

    // A real player's hello in guild chat is answered again GREET_REPLY_SECONDS after the last answer. Its own clock:
    // being greeted at login does not silence the answer to its hello, and a new login starts it over.
    constexpr time_t GREET_REPLY_SECONDS = 60;
    inline bool GreetReplyDue(time_t lastReply, time_t now)
    {
        return !lastReply || now - lastReply >= GREET_REPLY_SECONDS;
    }

    // How many guildmates answer a hello or a goodbye in guild chat: 2 or 3 (extra 0 or 1), fewer only when fewer are
    // online.
    inline size_t AnswerCount(size_t online, uint32_t extra)
    {
        return std::min<size_t>(online, 2 + std::min<uint32_t>(extra, 1));
    }

    // The mates beyond the first "speakers" stand in for them, in turn: speaker k gets mates speakers + k,
    // speakers + k + speakers... (a mate stands in for one speaker only, so nobody answers twice).
    inline std::vector<size_t> StandIns(size_t speaker, size_t speakers, size_t online)
    {
        std::vector<size_t> out;
        if (!speakers)
            return out;
        for (size_t i = speakers + speaker; i < online; i += speakers)
            out.push_back(i);
        return out;
    }

    // When the answers come: each 3 to 10 seconds after the hello, sorted, at least MIN_GAP seconds apart.
    inline std::vector<time_t> AnswerTimes(std::vector<time_t> times, time_t minGap = 2)
    {
        std::sort(times.begin(), times.end());
        for (size_t i = 1; i < times.size(); ++i)
            times[i] = std::max(times[i], times[i - 1] + minGap);
        return times;
    }

    // A goodbye in guild chat: a short line (5 words at most) with a goodbye word or phrase ("gn", "bye", "cya",
    // "bonne nuit", "a+", "++"...), not a hello.
    inline bool IsGoodbye(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        std::string words = " ";
        size_t count = 0;
        bool inWord = false;
        for (size_t i = 0; i < text.size(); ++i)
        {
            unsigned char const c = text[i];
            // "a+" and "++" are words here.
            bool const doubled = (i + 1 < text.size() && text[i + 1] == '+') || (i && text[i - 1] == '+');
            bool const afterA = i && text[i - 1] == 'a' && (i < 2 || !std::isalnum((unsigned char)text[i - 2]));
            bool const plus = c == '+' && (doubled || afterA);
            if ((c < 128 && std::isalnum(c)) || plus)
            {
                words += char(c);
                if (!inWord)
                    ++count;
                inWord = true;
            }
            else if (inWord)
            {
                words += ' ';
                inWord = false;
            }
        }
        if (inWord)
            words += ' ';
        if (!count || count > 5)
            return false;
        static char const* const phrases[] = { " gn ", " gnight ", " goodnight ", " good night ", " night all ",
                                               " nite ", " bye ", " byebye ", " bye bye ", " cya ", " cu ", " seeya ",
                                               " see ya ", " see you ", " later all ", " laters ", " gtg ", " g2g ",
                                               " logging off ", " off for tonight ", " bonne nuit ", " bn ", " a+ ",
                                               " ++ ", " a plus ", " a demain ", " ciao ", " tchao ", " bisous ",
                                               " je deco ", " je me deco ", " bonne soiree ", " au revoir " };
        for (char const* p : phrases)
            if (words.find(p) != std::string::npos)
                return true;
        return false;
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
