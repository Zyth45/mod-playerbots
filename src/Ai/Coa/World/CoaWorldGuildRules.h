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
        // French accents as plain letters (UTF-8 "é" is "e"), so "fabriqué" and "fabrique" read the same.
        std::string folded;
        folded.reserve(text.size());
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            unsigned char const c = static_cast<unsigned char>(text[i]);
            if (c == 0xC3 && i + 1 < text.size())
            {
                unsigned char const n = static_cast<unsigned char>(text[i + 1]) | 0x20;  // upper as lower
                char plain = 0;
                if (n >= 0xA0 && n <= 0xA5) plain = 'a';
                else if (n == 0xA7) plain = 'c';
                else if (n >= 0xA8 && n <= 0xAB) plain = 'e';
                else if (n >= 0xAC && n <= 0xAF) plain = 'i';
                else if (n >= 0xB2 && n <= 0xB6) plain = 'o';
                else if (n >= 0xB9 && n <= 0xBC) plain = 'u';
                if (plain)
                {
                    folded += plain;
                    ++i;
                    continue;
                }
            }
            folded += char(c);
        }
        text = folded;
        // Lower case, punctuation as spaces ("anyone?" is "anyone", "j'ai" is "j ai").
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char c) { return std::isalnum(c) ? char(std::tolower(c)) : ' '; });
        // One space between words ("quelqu’un", with its three-byte apostrophe, is "quelqu un").
        text.erase(std::unique(text.begin(), text.end(), [](char a, char b) { return a == ' ' && b == ' '; }),
                   text.end());
        text = " " + text + " ";

        // English, then French ("besoin de 20 [x]", "j'ai besoin de [x]", "je cherche [x]", "qui peut faire [x] ?",
        // "quelqu'un a [x] ?", "il me faut [x]"). Not "regarde [x]" nor "j'ai eu [x]": no word of asking.
        static char const* const asking[] = { " need ", " anyone ", " anybody ", " someone ", " somebody ", " craft ",
                                              " make ", " lf ", " looking for ", " spare ", " can i get ", " wtb ",
                                              " got a ", " has a ", " have a ", " have an ",
                                              " besoin ", " cherche ", " recherche ", " il me faut ", " il m en faut ",
                                              " me faudrait ", " qui peut ", " qui pourrait ", " qui sait faire ",
                                              " qui aurait ", " qui vend ", " fabriquer ", " crafter ",
                                              " quelqu un a ", " quelqu un aurait ", " quelqu un peut ",
                                              " quelqu un pourrait ", " quelqu un pour ", " qqn a ", " qqn aurait ",
                                              " qqn peut ", " qqun a ", " qq1 a " };
        for (char const* word : asking)
            if (text.find(word) != std::string::npos)
                return item;
        return 0;
    }

    // Most a guild order asks for at once (as the addon's GORD).
    constexpr std::uint32_t MaxOrderQty = 20;

    // The count written next to the item link in an order ("need 20 [x]", "20x [x]", "[x] x20", "[x] 20"): 1 to
    // MaxOrderQty, else 0 (any). A number elsewhere in the line ("[x] for my level 30 quest") is not a count.
    inline std::uint32_t OrderedQty(std::string const& message)
    {
        std::size_t link = message.find("|Hitem:");
        if (link == std::string::npos)
            return 0;
        // The colour code before the link belongs to it ("|cff1eff00|Hitem:...").
        if (link >= 10 && message.compare(link - 10, 2, "|c") == 0)
            link -= 10;
        std::size_t const close = message.find("|h|r", link);
        if (close == std::string::npos)
            return 0;

        auto counted = [](std::string const& digits) -> std::uint32_t
        {
            if (digits.empty() || digits.size() > 3)
                return 0;
            std::uint32_t const qty = std::uint32_t(std::strtoul(digits.c_str(), nullptr, 10));
            return qty >= 1 && qty <= MaxOrderQty ? qty : 0;
        };

        // Before the link: "20 ", "20x ", "x20 ", the number standing as a word.
        std::size_t i = link;
        while (i && message[i - 1] == ' ')
            --i;
        if (i && (message[i - 1] == 'x' || message[i - 1] == 'X') && i >= 2 && std::isdigit((unsigned char)message[i - 2]))
            --i;
        std::size_t start = i;
        while (start && std::isdigit((unsigned char)message[start - 1]))
            --start;
        if (start < i && (!start || message[start - 1] == ' ' || message[start - 1] == 'x' || message[start - 1] == 'X'))
            if (std::uint32_t qty = counted(message.substr(start, i - start)))
                return qty;

        // After the link: " x20", " 20", " 20x", the number standing as a word.
        std::size_t j = close + 4;
        while (j < message.size() && message[j] == ' ')
            ++j;
        if (j < message.size() && (message[j] == 'x' || message[j] == 'X'))
            ++j;
        std::size_t end = j;
        while (end < message.size() && std::isdigit((unsigned char)message[end]))
            ++end;
        if (end > j && (end == message.size() || !std::isalnum((unsigned char)message[end]) || message[end] == 'x' ||
                        message[end] == 'X'))
            return counted(message.substr(j, end - j));
        return 0;
    }

    // How a guildmate crafts an order: casts of the recipe (made items each) to cover qty, one stack at most (one
    // cast when the order gives no count). The guild's mail holds one stack. A recipe whose materials cost nothing
    // (no vendor price) stays at one cast, as before counts were read: nothing free twenty times over.
    struct GuildCraft
    {
        std::uint32_t casts;
        std::uint32_t sent;
    };
    inline GuildCraft PlanGuildCraft(std::uint32_t qty, std::uint32_t made, std::uint32_t maxStack, std::uint64_t castCost = 1)
    {
        made = std::max<std::uint32_t>(1, made);
        maxStack = std::max<std::uint32_t>(1, maxStack);
        if (!castCost)
            return { 1, std::min(std::min(made, maxStack), qty ? qty : made) };
        std::uint32_t const want = qty ? std::min(qty, maxStack) : std::min(made, maxStack);
        std::uint32_t const casts = (want + made - 1) / made;
        return { casts, std::min(casts * made, maxStack) };
    }

    // The combined order (3.1.4): what is still missing of an order of qty after got (0: any count, done at the
    // first item).
    inline std::uint32_t OrderMissing(std::uint32_t qty, std::uint32_t got) { return qty ? (got < qty ? qty - got : 0) : (got ? 0 : 1); }
    // The count a path is asked for: what is missing, or 0 (any) for an order without a count.
    inline std::uint32_t OrderAsk(std::uint32_t qty, std::uint32_t got) { return qty ? OrderMissing(qty, got) : 0; }

    // What one holder gives of its stacks for need items (0: any, the one biggest stack): the biggest stacks first,
    // whole while they fit, then part of the next (a stack splits, never more than need); maxStacks at most (a mail).
    // Each pick is {stack index, count taken}.
    struct StackTake
    {
        std::size_t index;
        std::uint32_t count;
    };
    inline std::vector<StackTake> TakeStacks(std::vector<std::uint32_t> const& stacks, std::uint32_t need, std::size_t maxStacks)
    {
        std::vector<StackTake> picks;
        std::vector<std::size_t> order;
        for (std::size_t i = 0; i < stacks.size(); ++i)
            if (stacks[i])
                order.push_back(i);
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return stacks[a] > stacks[b]; });
        std::uint32_t got = 0;
        for (std::size_t i : order)
        {
            if (picks.size() >= maxStacks || (need && got >= need))
                break;
            std::uint32_t const take = need ? std::min(stacks[i], need - got) : stacks[i];
            picks.push_back({ i, take });
            got += take;
            if (!need)
                break;
        }
        return picks;
    }

    // The guild bank stacks an order may take out of a tab for this player (3.1.4): never more than the player's own
    // rank could withdraw there today (a guild order is no way round the guild's rights). The guild master: all; no
    // right to see the tab: none; slotsPerDay < 0 or huge (the core's unlimited): all; else what is left of the day.
    inline std::uint32_t BankStacksAllowed(bool leader, bool viewTab, std::int64_t slotsPerDay, std::int64_t usedToday)
    {
        if (leader)
            return 0xFFFFFFFFu;
        if (!viewTab || !slotsPerDay)
            return 0;
        if (slotsPerDay < 0 || slotsPerDay >= 0x7FFFFFFF)
            return 0xFFFFFFFFu;
        return slotsPerDay > usedToday ? std::uint32_t(slotsPerDay - usedToday) : 0;
    }

    // The holders of a stock (guildmates' bags and banks) asked for need items: the best stocked first, each gives
    // what it has up to what is still missing. Each pick is {holder index, count}; 0 (any): the best stocked alone.
    inline std::vector<StackTake> SplitStock(std::vector<std::uint32_t> const& held, std::uint32_t need)
    {
        std::vector<StackTake> picks;
        std::vector<std::size_t> order;
        for (std::size_t i = 0; i < held.size(); ++i)
            if (held[i])
                order.push_back(i);
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return held[a] > held[b]; });
        std::uint32_t got = 0;
        for (std::size_t i : order)
        {
            if (need && got >= need)
                break;
            std::uint32_t const take = need ? std::min(held[i], need - got) : held[i];
            picks.push_back({ i, take });
            got += take;
            if (!need)
                break;
        }
        return picks;
    }

    // The addon's catalogue category (GCAT, 3.1.4) of an item class: "trade" (trade goods 7, reagents 5, gems 3,
    // recipes 9), "consumable" (0), "equipment" (weapons 2, armor 4), "other"; an empty or unknown category: all.
    inline bool InCatalogCategory(std::uint32_t itemClass, std::string const& category)
    {
        bool const trade = itemClass == 7 || itemClass == 5 || itemClass == 3 || itemClass == 9;
        bool const consumable = itemClass == 0;
        bool const equipment = itemClass == 2 || itemClass == 4;
        if (category == "trade")
            return trade;
        if (category == "consumable")
            return consumable;
        if (category == "equipment")
            return equipment;
        if (category == "other")
            return !trade && !consumable && !equipment;
        return true;
    }

    // Professions a real player's guild keeps online (3.1.4, Guilds.PlayerGuildProfessions): a bit each.
    // Mining 186, Herbalism 182, Skinning 393, Blacksmithing 164, Alchemy 171, Tailoring 197, Leatherworking 165,
    // Enchanting 333, Engineering 202, Cooking 185, First Aid 129.
    inline std::uint32_t ProfessionBit(std::uint32_t skill)
    {
        static constexpr std::uint32_t skills[] = { 186, 182, 393, 164, 171, 197, 165, 333, 202, 185, 129 };
        for (std::uint32_t i = 0; i < sizeof(skills) / sizeof(skills[0]); ++i)
            if (skills[i] == skill)
                return 1u << i;
        return 0;
    }
    inline std::uint32_t BitCount(std::uint32_t v)
    {
        std::uint32_t n = 0;
        for (; v; v &= v - 1)
            ++n;
        return n;
    }
    // The offline members to bring online first so that every profession some member has is online: each time the
    // one who adds the most professions still missing (the first of equals), until none adds any.
    inline std::vector<std::size_t> ProfessionCover(std::uint32_t onlineMask, std::vector<std::uint32_t> const& offline)
    {
        std::vector<std::size_t> picks;
        std::vector<bool> taken(offline.size(), false);
        std::uint32_t covered = onlineMask;
        for (;;)
        {
            std::size_t best = offline.size();
            std::uint32_t bestGain = 0;
            for (std::size_t i = 0; i < offline.size(); ++i)
                if (!taken[i])
                    if (std::uint32_t const gain = BitCount(offline[i] & ~covered); gain > bestGain)
                    {
                        best = i;
                        bestGain = gain;
                    }
            if (best == offline.size())
                return picks;
            taken[best] = true;
            picks.push_back(best);
            covered |= offline[best];
        }
    }
    // The skill of a profession bit, its starter spell (the apprentice rank a trainer teaches), and whether it takes
    // one of the two primary slots (cooking and first aid do not).
    inline std::uint32_t ProfessionSkill(std::uint32_t bit)
    {
        static constexpr std::uint32_t skills[] = { 186, 182, 393, 164, 171, 197, 165, 333, 202, 185, 129 };
        for (std::uint32_t i = 0; i < sizeof(skills) / sizeof(skills[0]); ++i)
            if (bit == (1u << i))
                return skills[i];
        return 0;
    }
    inline std::uint32_t ProfessionStarterSpell(std::uint32_t skill)
    {
        switch (skill)
        {
            case 186: return 2575;  // Mining
            case 182: return 2366;  // Herbalism
            case 393: return 8613;  // Skinning
            case 164: return 2018;  // Blacksmithing
            case 171: return 2259;  // Alchemy
            case 197: return 3908;  // Tailoring
            case 165: return 2108;  // Leatherworking
            case 333: return 7411;  // Enchanting
            case 202: return 4036;  // Engineering
            case 185: return 2550;  // Cooking
            case 129: return 3273;  // First Aid
            default: return 0;
        }
    }
    inline bool IsPrimaryProfession(std::uint32_t skill) { return skill && skill != 185 && skill != 129; }
    // The one profession a guild learns next (3.1.4, Guilds.PlayerGuildTeach): the first that no member has, online
    // or not, and none was taught already (0: none missing). The bit, gathering first (the list's order).
    inline std::uint32_t ProfessionToTeach(std::uint32_t membersMask, std::uint32_t taughtMask)
    {
        for (std::uint32_t i = 0; i < 11; ++i)
            if (!((membersMask | taughtMask) & (1u << i)))
                return 1u << i;
        return 0;
    }
    // A member who may learn it: a free primary slot for a primary profession (two at most), anyone for cooking and
    // first aid, never one who has it.
    inline bool MayLearnProfession(std::uint32_t skill, std::uint32_t primaryCount, std::uint32_t maxPrimary, bool hasIt)
    {
        if (!skill || hasIt)
            return false;
        return !IsPrimaryProfession(skill) || primaryCount < maxPrimary;
    }

    // An online member is the only one online with one of its professions: it stays when its time is up.
    inline bool SoleProfessionHolder(std::uint32_t mine, std::uint32_t othersOnline) { return (mine & ~othersOnline) != 0; }

    // An auction a guild order may buy out: its stack and its buyout.
    struct AuctionOffer
    {
        std::uint32_t count;
        std::uint64_t buyout;
    };
    // The auctions bought for an order of qty (0: any count, the one cheapest auction as before): the cheapest by
    // unit first while they fit the count, then the cheapest one for what is still missing (never more than one stack
    // over); at most maxCopper in all and maxAuctions auctions (one mail).
    inline std::vector<std::size_t> PickAuctions(std::vector<AuctionOffer> const& offers, std::uint32_t qty,
                                                 std::uint64_t maxCopper, std::size_t maxAuctions)
    {
        std::vector<std::size_t> picked;
        if (!maxAuctions)
            return picked;
        std::vector<bool> taken(offers.size(), false);
        std::uint64_t spent = 0;
        std::uint32_t got = 0;
        if (qty)
        {
            std::vector<std::size_t> order;
            for (std::size_t i = 0; i < offers.size(); ++i)
                if (offers[i].count)
                    order.push_back(i);
            std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b)
                             { return offers[a].buyout * offers[b].count < offers[b].buyout * offers[a].count; });
            for (std::size_t i : order)
            {
                if (got >= qty || picked.size() >= maxAuctions)
                    break;
                if (got + offers[i].count <= qty && spent + offers[i].buyout <= maxCopper)
                {
                    taken[i] = true;
                    picked.push_back(i);
                    spent += offers[i].buyout;
                    got += offers[i].count;
                }
            }
            if (got >= qty || picked.size() >= maxAuctions)
                return picked;
        }
        // The cheapest auction left for what is missing (or the one auction of an order without a count).
        std::size_t best = offers.size();
        for (std::size_t i = 0; i < offers.size(); ++i)
            if (!taken[i] && offers[i].count && spent + offers[i].buyout <= maxCopper &&
                (best == offers.size() || offers[i].buyout < offers[best].buyout))
                best = i;
        if (best < offers.size())
            picked.push_back(best);
        return picked;
    }

    // What the materials cost a guildmate who crafts for you: each reagent at its vendor price, or four times what a
    // vendor gives for it when no vendor sells it (cloth, ore, herbs). No fee between guildmates.
    inline std::uint64_t ReagentPrice(std::uint32_t buyPrice, std::uint32_t sellPrice)
    {
        return buyPrice ? buyPrice : std::uint64_t(sellPrice) * 4;
    }

    // 3.1.4: what one reagent of a guild craft costs. What it costs an artisan (its market price, or less when making
    // it is cheaper), never more than the 3.1.3 price (ReagentPrice): a guildmate does not charge the house's market
    // markup. No market price known (0): the 3.1.3 price.
    inline std::uint64_t GuildCraftReagentPrice(std::uint64_t artisanCost, std::uint32_t buyPrice, std::uint32_t sellPrice)
    {
        std::uint64_t const old = ReagentPrice(buyPrice, sellPrice);
        if (!artisanCost)
            return old;
        return old && old < artisanCost ? old : artisanCost;
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
