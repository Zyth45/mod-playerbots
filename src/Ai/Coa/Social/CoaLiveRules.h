/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COALIVERULES_H
#define PLAYERBOTS_COALIVERULES_H

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// CoA Live (3.0): what bots say and do, by situation, from the bank of CoA IA MIND (tables playerbots_coa_live_*,
// spec BANQUE-LIVE-3.0). Pure rules: the bank, the draw, the pace, the human touch (typing time, typos, slang) and
// the reading of a whisper. No game object, so that they can be tested alone; CoaLive.cpp feeds them.

namespace CoaLive
{
enum Channel : uint8_t
{
    CH_NEAR = 0,     // /say or /emote next to the bot
    CH_GENERAL = 1,
    CH_TRADE = 2,
    CH_WORLD = 3,    // the world channel of the CoA client (ChatChannels id 1, "Ascension")
    CH_LFG = 4,
    CH_GUILD = 5,
    CH_WHISPER = 6,
    CH_PARTY = 10,
};

// The soul of a bot (contract ACCROCHES-LLM-3.0); none: every weight is 10.
enum Archetype : uint8_t
{
    ARCH_LAZY = 0,
    ARCH_QUESTER,
    ARCH_WANDERER,
    ARCH_TRADER,
    ARCH_PVP,
    ARCH_COUNT,
    ARCH_NONE = ARCH_COUNT,
};

struct Line
{
    uint32_t id = 0;
    std::string situation;
    uint8_t channel = 0;
    uint8_t locale = 0;
    uint8_t levelBand = 0;  // 0 any, 1 = 1-19, 2 = 20-39, 3 = 40-59, 4 = 60
    uint32_t emote = 0;     // EmotesText id, 0 none
    std::string text;       // empty: the emote alone
    uint8_t weight[ARCH_COUNT] = {10, 10, 10, 10, 10};
    bool rare = false;
    uint8_t race = 0;   // night bank filters (0: any): the bot's race, class, zone
    uint8_t cls = 0;
    uint32_t zone = 0;
};

// The bot a line is drawn for (the night bank's filters).
struct Who
{
    uint8_t race = 0;
    uint8_t cls = 0;
    uint32_t zone = 0;
};

struct Rule
{
    uint32_t chancePct = 0;
    uint32_t delayMinMs = 0;
    uint32_t delayMaxMs = 0;
    uint32_t botCdS = 0;
    uint32_t scopeCdS = 0;
    uint32_t scopePer10Min = 0;  // 0 none
    bool needPlayer = true;
};

inline uint8_t LevelBand(uint32_t level)
{
    if (level < 20)
        return 1;
    if (level < 40)
        return 2;
    if (level < 60)
        return 3;
    return 4;
}

// A small deterministic random source (the game side seeds it from urand), so that the rules can be tested.
struct Rng
{
    std::function<uint32_t(uint32_t, uint32_t)> between;  // inclusive
    bool Roll(uint32_t pct) const { return pct >= 100 || (pct > 0 && between(0, 99) < pct); }
};

// ---------------------------------------------------------------- text

// Every "%name" of the text, in order. A placeholder is '%' followed by lower-case letters.
inline std::vector<std::string> Placeholders(std::string const& text)
{
    std::vector<std::string> out;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] != '%')
            continue;
        size_t j = i + 1;
        while (j < text.size() && text[j] >= 'a' && text[j] <= 'z')
            ++j;
        if (j > i + 1)
            out.push_back(text.substr(i + 1, j - i - 1));
        i = j - 1;
    }
    return out;
}

// The text with its placeholders filled; false when one of them has no value (the line is then skipped). A longer
// name wins ("%subzone" is not "%s" + "ubzone").
inline bool Fill(std::string const& text, std::map<std::string, std::string> const& values, std::string& out)
{
    out.clear();
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] != '%')
        {
            out += text[i];
            continue;
        }
        size_t j = i + 1;
        while (j < text.size() && text[j] >= 'a' && text[j] <= 'z')
            ++j;
        if (j == i + 1)
        {
            out += '%';
            continue;
        }
        auto it = values.find(text.substr(i + 1, j - i - 1));
        if (it == values.end() || it->second.empty())
            return false;
        out += it->second;
        i = j - 1;
    }
    return true;
}

// The words of a template, placeholders and the rest kept apart: the human touch only changes plain words, never a
// filled placeholder, an item link or a name.
struct Token
{
    std::string text;
    bool word = false;  // plain letters, may be abbreviated or get a typo
};

inline bool AsciiLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

inline std::vector<Token> Tokenize(std::string const& text)
{
    std::vector<Token> out;
    size_t i = 0;
    while (i < text.size())
    {
        size_t j = i;
        if (text[i] == '%')
        {
            ++j;
            while (j < text.size() && text[j] >= 'a' && text[j] <= 'z')
                ++j;
            out.push_back({text.substr(i, j - i), false});
        }
        else if (AsciiLetter(text[i]) || static_cast<unsigned char>(text[i]) >= 0x80 || text[i] == '\'')
        {
            while (j < text.size() &&
                   (AsciiLetter(text[j]) || static_cast<unsigned char>(text[j]) >= 0x80 || text[j] == '\''))
                ++j;
            out.push_back({text.substr(i, j - i), true});
        }
        else
        {
            ++j;
            out.push_back({text.substr(i, 1), false});
        }
        i = j;
    }
    return out;
}

inline std::string Join(std::vector<Token> const& tokens)
{
    std::string out;
    for (Token const& t : tokens)
        out += t.text;
    return out;
}

inline std::string AsciiLower(std::string s)
{
    for (char& c : s)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// Slang: a whole word (or a phrase of whole words, "no problem") of the slang table becomes its short form, each
// at abbrevPct. Phrases are tried before single words.
inline void Abbreviate(std::vector<Token>& tokens, std::vector<std::pair<std::string, std::string>> const& slang,
                       uint32_t abbrevPct, Rng const& rng)
{
    if (slang.empty() || abbrevPct == 0)
        return;
    for (auto const& [full, shortForm] : slang)
    {
        // The phrase as tokens: words separated by single spaces.
        std::vector<std::string> words;
        size_t s = 0;
        while (s <= full.size())
        {
            size_t e = full.find(' ', s);
            if (e == std::string::npos)
                e = full.size();
            if (e > s)
                words.push_back(full.substr(s, e - s));
            s = e + 1;
        }
        if (words.empty())
            continue;
        size_t const span = words.size() * 2 - 1;  // word, space, word...
        for (size_t i = 0; i + span <= tokens.size(); ++i)
        {
            bool match = true;
            for (size_t k = 0; k < words.size() && match; ++k)
            {
                Token const& t = tokens[i + k * 2];
                match = t.word && AsciiLower(t.text) == words[k];
                if (match && k + 1 < words.size())
                    match = !tokens[i + k * 2 + 1].word && tokens[i + k * 2 + 1].text == " ";
            }
            if (!match || !rng.Roll(abbrevPct))
                continue;
            tokens[i] = {shortForm, false};
            tokens.erase(tokens.begin() + i + 1, tokens.begin() + i + span);
        }
    }
}

// The French letters a hurried player types without their accent (UTF-8, two bytes each).
inline bool StripOneAccent(std::string& word, size_t at)
{
    static std::pair<char const*, char> const accents[] = {
        {"\xC3\xA9", 'e'}, {"\xC3\xA8", 'e'}, {"\xC3\xAA", 'e'}, {"\xC3\xA0", 'a'}, {"\xC3\xA2", 'a'},
        {"\xC3\xB4", 'o'}, {"\xC3\xBB", 'u'}, {"\xC3\xB9", 'u'}, {"\xC3\xAE", 'i'}, {"\xC3\xA7", 'c'},
    };
    for (auto const& [utf8, plain] : accents)
        if (word.compare(at, 2, utf8) == 0)
        {
            word.replace(at, 2, 1, plain);
            return true;
        }
    return false;
}

// One typo at most in the line, in a plain word of 3 letters or more: two letters swapped, a letter dropped or
// doubled, an accent forgotten (French); or the capital or the final full stop of the line left out.
inline void Typo(std::vector<Token>& tokens, uint32_t typoPct, bool french, Rng const& rng)
{
    if (!rng.Roll(typoPct))
        return;

    uint32_t const kind = rng.between(0, french ? 5 : 4);
    if (kind == 4)  // the final full stop, or else the capital
    {
        if (!tokens.empty() && tokens.back().text == "." && !tokens.back().word)
        {
            tokens.pop_back();
            return;
        }
        for (Token& t : tokens)
            if (t.word && t.text[0] >= 'A' && t.text[0] <= 'Z')
            {
                t.text[0] = static_cast<char>(t.text[0] - 'A' + 'a');
                return;
            }
        return;
    }

    std::vector<size_t> candidates;
    for (size_t i = 0; i < tokens.size(); ++i)
        if (tokens[i].word && tokens[i].text.size() >= 3)
            candidates.push_back(i);
    if (candidates.empty())
        return;
    std::string& w = tokens[candidates[rng.between(0, static_cast<uint32_t>(candidates.size()) - 1)]].text;

    if (kind == 5)  // an accent forgotten
    {
        for (size_t i = 0; i + 1 < w.size(); ++i)
            if (StripOneAccent(w, i))
                return;
        return;
    }

    // Letters only (ASCII), never the first one: a name-like word keeps its look.
    std::vector<size_t> pos;
    for (size_t i = 1; i < w.size(); ++i)
        if (AsciiLetter(w[i]))
            pos.push_back(i);
    if (pos.empty())
        return;
    size_t const p = pos[rng.between(0, static_cast<uint32_t>(pos.size()) - 1)];
    switch (kind)
    {
        case 0:  // swapped with the next ASCII letter
            if (p + 1 < w.size() && AsciiLetter(w[p + 1]) && w[p] != w[p + 1])
                std::swap(w[p], w[p + 1]);
            else if (p > 1 && AsciiLetter(w[p - 1]) && w[p] != w[p - 1])
                std::swap(w[p], w[p - 1]);
            break;
        case 1:  // dropped
            w.erase(p, 1);
            break;
        default:  // doubled, unless it would make three in a row
            if (!(p + 1 < w.size() && w[p + 1] == w[p]) && !(w[p - 1] == w[p]))
                w.insert(p, 1, w[p]);
            break;
    }
}

// The time a player takes to think and type the line.
inline uint32_t TypingMs(Rule const& rule, size_t length, uint32_t msPerChar, Rng const& rng)
{
    uint32_t const lo = std::min(rule.delayMinMs, rule.delayMaxMs), hi = std::max(rule.delayMinMs, rule.delayMaxMs);
    return rng.between(lo, hi) + static_cast<uint32_t>(length) * msPerChar;
}

// ---------------------------------------------------------------- whispers

// Whether CoA Live takes a whisper. Off (AiPlayerbot.CoaLive.Enable = 0), never: every whisper goes exactly where
// it went before 3.0 (the commands, then the old free chat replies). On, only a real player's whisper to a random
// bot that is not its own master's.
inline bool TakesWhisper(bool enabled, bool fromRealPlayer, bool toRandomBot, bool fromIsMaster)
{
    return enabled && fromRealPlayer && toRandomBot && !fromIsMaster;
}

struct Intent
{
    uint32_t priority = 0;
    std::string intent;
    std::vector<std::string> keywords;  // each padded with spaces: " how much "
};

inline std::vector<std::string> SplitKeywords(std::string const& list)
{
    std::vector<std::string> out;
    size_t s = 0;
    while (s <= list.size())
    {
        size_t e = list.find(',', s);
        if (e == std::string::npos)
            e = list.size();
        std::string k = list.substr(s, e - s);
        size_t const a = k.find_first_not_of(' '), b = k.find_last_not_of(' ');
        if (a != std::string::npos)
            out.push_back(" " + k.substr(a, b - a + 1) + " ");
        s = e + 1;
    }
    return out;
}

// A whisper made ready for the keywords: already in lower case (the game side lowers UTF-8 with the core), every
// character that is neither a letter nor a digit becomes a space (the apostrophe too: "t'es" -> "t es"), bytes of a
// UTF-8 letter kept, single spaces, one at each end.
inline std::string NormalizeWhisper(std::string const& lowered)
{
    std::string out = " ";
    for (char c : lowered)
    {
        bool const keep = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || static_cast<unsigned char>(c) >= 0x80;
        if (keep)
            out += c;
        else if (out.back() != ' ')
            out += ' ';
    }
    if (out.back() != ' ')
        out += ' ';
    return out;
}

// The intent of a normalized whisper: the first intent (by priority) one of whose keywords is in it, else
// "fallback".
inline std::string Classify(std::string const& normalized, std::vector<Intent> const& intents)
{
    for (Intent const& intent : intents)
        for (std::string const& k : intent.keywords)
            if (normalized.find(k) != std::string::npos)
                return intent.intent;
    return "fallback";
}

// A whisper that says yes to the bot's own Trade announce (WTS: the item goes by mail, cash on delivery): only a
// clear answer. A trade or craft intent ("wts", "how much"), a link of the offered item, or a word of agreement.
// Anything else ("looking for a guild?", "lol") is answered as any whisper and the offer stays open.
// normalized: NormalizeWhisper; lowered: the whisper in lower case, item links kept ("|hitem:<id>:").
inline bool AcceptsOffer(std::string const& intent, std::string const& normalized, std::string const& lowered,
                         uint32_t itemId)
{
    if (intent == "trade" || intent == "craft")
        return true;
    if (itemId && lowered.find("|hitem:" + std::to_string(itemId) + ":") != std::string::npos)
        return true;
    static char const* const yes[] = {" yes ", " yep ", " yeah ", " sure ", " ok ", " okay ", " deal ", " take it ",
                                      " send it ", " cod ", " oui ", " ouais ", " je prends ", " je le prends ",
                                      " envoie ", " d accord ", " dac "};
    for (char const* word : yes)
        if (normalized.find(word) != std::string::npos)
            return true;
    return false;
}

// "where is goldshire?": the words asked about after the "where" keyword that matched (padded " where "), longest
// first, at most four words, leading little words left out ("is the", "est le"...). Empty when nothing follows.
inline std::vector<std::string> WhereSubjects(std::string const& normalized, std::vector<std::string> const& keywords)
{
    static char const* const little[] = {"is", "s", "are", "the", "a", "an", "to", "get", "find", "i", "can", "u",
                                         "est", "se", "trouve", "le", "la", "les", "l", "un", "une", "du", "de", "des",
                                         "d", "on", "va", "aller", "trouver"};
    size_t best = std::string::npos, after = 0;
    for (std::string const& k : keywords)
    {
        size_t const at = normalized.find(k);
        if (at != std::string::npos && (best == std::string::npos || at < best))
        {
            best = at;
            after = at + k.size();
        }
    }
    std::vector<std::string> out;
    if (best == std::string::npos)
        return out;
    std::vector<std::string> words;
    size_t i = after;
    while (i < normalized.size())
    {
        size_t const j = normalized.find(' ', i);
        std::string const w = normalized.substr(i, (j == std::string::npos ? normalized.size() : j) - i);
        if (!w.empty())
            words.push_back(w);
        if (j == std::string::npos)
            break;
        i = j + 1;
    }
    size_t start = 0;
    while (start < words.size() &&
           std::find_if(std::begin(little), std::end(little), [&](char const* l) { return words[start] == l; }) !=
               std::end(little))
        ++start;
    size_t const n = std::min<size_t>(4, words.size() - start);
    for (size_t k = n; k > 0; --k)
    {
        std::string s;
        for (size_t w = start; w < start + k; ++w)
            s += (w > start ? " " : "") + words[w];
        out.push_back(s);
    }
    return out;
}

// ---------------------------------------------------------------- bank

// A conversation of the night bank: its steps in order, said by 2 or 3 different bots (speaker 1, 2, 3).
struct ThreadStep
{
    uint8_t speaker = 1;
    std::string text;
};

struct Thread
{
    uint8_t levelBand = 0;
    uint8_t race = 0;
    uint8_t cls = 0;
    uint32_t zone = 0;
    std::vector<ThreadStep> steps;

    uint8_t Speakers() const
    {
        uint8_t n = 0;
        for (ThreadStep const& s : steps)
            n = std::max(n, s.speaker);
        return n;
    }
};

struct Bank
{
    // channel -> locale -> conversations (night bank)
    std::map<uint8_t, std::map<uint8_t, std::vector<Thread>>> threads;
    std::map<std::string, Rule> rules;
    // situation -> locale -> lines
    std::map<std::string, std::map<uint8_t, std::vector<Line>>> lines;
    std::map<uint8_t, std::vector<Intent>> intents;  // sorted by priority
    std::map<uint8_t, std::vector<std::pair<std::string, std::string>>> slang;  // longest phrases first

    void Add(Line line) { lines[line.situation][line.locale].push_back(std::move(line)); }

    void AddIntent(uint8_t locale, uint32_t priority, std::string const& intent, std::string const& keywords)
    {
        auto& v = intents[locale];
        v.push_back({priority, intent, SplitKeywords(keywords)});
        std::sort(v.begin(), v.end(), [](Intent const& a, Intent const& b) { return a.priority < b.priority; });
    }

    void AddSlang(uint8_t locale, std::string const& full, std::string const& shortForm)
    {
        auto& v = slang[locale];
        v.emplace_back(AsciiLower(full), shortForm);
        std::stable_sort(v.begin(), v.end(), [](auto const& a, auto const& b) { return a.first.size() > b.first.size(); });
    }

    Rule const* RuleOf(std::string const& situation) const
    {
        auto it = rules.find(situation);
        return it == rules.end() ? nullptr : &it->second;
    }

    // The lines of the situation in this locale, or in English when the locale has none (spec §6).
    std::vector<Line> const* LinesOf(std::string const& situation, uint8_t locale) const
    {
        auto s = lines.find(situation);
        if (s == lines.end())
            return nullptr;
        auto l = s->second.find(locale);
        if (l != s->second.end() && !l->second.empty())
            return &l->second;
        l = s->second.find(0);
        return l != s->second.end() && !l->second.empty() ? &l->second : nullptr;
    }

    std::vector<Intent> const& IntentsOf(uint8_t locale) const
    {
        static std::vector<Intent> const none;
        auto it = intents.find(locale);
        if (it != intents.end() && !it->second.empty())
            return it->second;
        it = intents.find(0);
        return it != intents.end() ? it->second : none;
    }

    std::vector<std::pair<std::string, std::string>> const& SlangOf(uint8_t locale) const
    {
        static std::vector<std::pair<std::string, std::string>> const none;
        auto it = slang.find(locale);
        return it != slang.end() ? it->second : none;
    }

    size_t LineCount() const
    {
        size_t n = 0;
        for (auto const& [s, byLocale] : lines)
            for (auto const& [l, v] : byLocale)
                n += v.size();
        return n;
    }
};

// A ring of the last line ids said (by a bot, or in a channel): a line in it is left out of the draw.
class Ring
{
public:
    explicit Ring(size_t size = 50) : size_(size) {}
    void SetSize(size_t size)
    {
        size_ = size;
        while (ids_.size() > size_)
            ids_.pop_front();
    }
    bool Has(uint32_t id) const { return std::find(ids_.begin(), ids_.end(), id) != ids_.end(); }
    void Push(uint32_t id)
    {
        if (size_ == 0)
            return;
        ids_.push_back(id);
        while (ids_.size() > size_)
            ids_.pop_front();
    }
    size_t Size() const { return ids_.size(); }

private:
    size_t size_;
    std::deque<uint32_t> ids_;
};

struct Pick
{
    Line const* line = nullptr;
    std::string text;  // filled, before the human touch
};

// The draw: the lines of the situation (locale, then English), of the bot's level band or of any band, outside both
// rings, rare lines only when rareRoll; weighted by the bot's soul. A line whose placeholders do not all fill is
// skipped and the draw goes on with the others.
inline Pick Draw(Bank const& bank, std::string const& situation, uint8_t locale, uint8_t levelBand, Archetype soul,
                 bool rareRoll, Ring const* botRing, Ring const* channelRing,
                 std::map<std::string, std::string> const& values, Rng const& rng, Who const& who = Who{})
{
    Pick pick;
    std::vector<Line> const* all = bank.LinesOf(situation, locale);
    if (!all)
        return pick;

    std::vector<Line const*> pool;
    for (Line const& line : *all)
    {
        if (line.levelBand && line.levelBand != levelBand)
            continue;
        if ((line.race && line.race != who.race) || (line.cls && line.cls != who.cls) ||
            (line.zone && line.zone != who.zone))
            continue;
        if (line.rare && !rareRoll)
            continue;
        if ((botRing && botRing->Has(line.id)) || (channelRing && channelRing->Has(line.id)))
            continue;
        pool.push_back(&line);
    }

    while (!pool.empty())
    {
        uint32_t total = 0;
        for (Line const* l : pool)
            total += soul == ARCH_NONE ? 10u : l->weight[soul];
        if (total == 0)
            return pick;
        uint32_t roll = rng.between(0, total - 1);
        size_t chosen = 0;
        for (; chosen < pool.size(); ++chosen)
        {
            uint32_t const w = soul == ARCH_NONE ? 10u : pool[chosen]->weight[soul];
            if (roll < w)
                break;
            roll -= w;
        }
        Line const* line = pool[std::min(chosen, pool.size() - 1)];
        std::string filled;
        if (Fill(line->text, values, filled))
        {
            pick.line = line;
            pick.text = filled;
            return pick;
        }
        pool.erase(std::find(pool.begin(), pool.end(), line));
    }
    return pick;
}

// ---------------------------------------------------------------- Trade pace

// AiPlayerbot.CoaLive.TradeMessagesPerMinute: the gap before the next Trade line, 30 to 90 s for one a minute (the
// mean is 60 s / rate), never under 5 s.
inline uint32_t TradeGapMs(uint32_t perMinute, Rng const& rng)
{
    if (!perMinute)
        return 0;
    uint32_t const lo = std::max<uint32_t>(5000, 30000 / perMinute);
    uint32_t const hi = std::max<uint32_t>(lo, 90000 / perMinute);
    return rng.between(lo, hi);
}

// A speaker for the next Trade line: not one of the last ones heard (recent, newest last), else anyone.
inline size_t PickSpeaker(size_t candidates, std::vector<uint64_t> const& ids, std::vector<uint64_t> const& recent,
                          Rng const& rng)
{
    if (!candidates)
        return SIZE_MAX;
    std::vector<size_t> fresh;
    for (size_t i = 0; i < candidates && i < ids.size(); ++i)
        if (std::find(recent.begin(), recent.end(), ids[i]) == recent.end())
            fresh.push_back(i);
    if (fresh.empty())
        return rng.between(0, uint32_t(candidates - 1));
    return fresh[rng.between(0, uint32_t(fresh.size() - 1))];
}

// ---------------------------------------------------------------- night bank

// The night bank (playerbots_coa_night_lines, AiPlayerbot.CoaLive.NightBank.Enable) speaks on CoA Live's channels
// (1 zone, 3 world, 10 group). Its single lines go into the bank under a situation of their own; "": not used.
inline std::string NightSituation(uint8_t channel, std::string const& event)
{
    if (event == "thread")
        return "";
    if (channel == CH_GENERAL)
        return "night_zone";
    if (channel == CH_WORLD)
        return "night_world";
    if (channel == CH_PARTY && event.rfind("dgn_", 0) == 0)
        return "night_" + event;
    return "";
}

// The pace of the night situations: the bank's rule when it has one, else the zone chatter's ("general_chat") for
// the channels and the boss pull's for the group.
inline void AddNightRules(Bank& bank)
{
    Rule chatter{100, 2000, 6000, 600, 120, 0, true};
    if (Rule const* r = bank.RuleOf("general_chat"))
        chatter = *r;
    Rule party{50, 1500, 4000, 300, 60, 0, true};
    if (Rule const* r = bank.RuleOf("boss_pull"))
        party = *r;
    for (char const* s : {"night_zone", "night_world", "night_thread"})
        bank.rules.emplace(s, chatter);
    for (auto const& [situation, byLocale] : bank.lines)
        if (situation.rfind("night_dgn_", 0) == 0)
            bank.rules.emplace(situation, party);
}

// A conversation for these bots: their locale (else English), their band, zone, race and class, no more speakers
// than bots at hand. Null: none.
inline Thread const* DrawThread(Bank const& bank, uint8_t channel, uint8_t locale, uint8_t levelBand, Who const& who,
                                size_t bots, Rng const& rng)
{
    auto c = bank.threads.find(channel);
    if (c == bank.threads.end())
        return nullptr;
    auto l = c->second.find(locale);
    if (l == c->second.end() || l->second.empty())
        l = c->second.find(0);
    if (l == c->second.end())
        return nullptr;
    std::vector<Thread const*> pool;
    for (Thread const& t : l->second)
    {
        if (t.steps.empty() || t.Speakers() < 1 || t.Speakers() > bots)
            continue;
        if ((t.levelBand && t.levelBand != levelBand) || (t.zone && t.zone != who.zone) ||
            (t.race && t.race != who.race) || (t.cls && t.cls != who.cls))
            continue;
        pool.push_back(&t);
    }
    return pool.empty() ? nullptr : pool[rng.between(0, uint32_t(pool.size() - 1))];
}

// The human touch on a line template: slang, then at most one typo, then the placeholders filled. False when a
// placeholder has no value.
inline bool Humanize(std::string const& text, std::map<std::string, std::string> const& values,
                     std::vector<std::pair<std::string, std::string>> const& slang, uint32_t abbrevPct, uint32_t typoPct,
                     bool french, Rng const& rng, std::string& out)
{
    std::vector<Token> tokens = Tokenize(text);
    Abbreviate(tokens, slang, abbrevPct, rng);
    Typo(tokens, typoPct, french, rng);
    return Fill(Join(tokens), values, out);
}

// ---------------------------------------------------------------- pace

// The cooldowns of the rules: a bot does not repeat a situation within botCdS; in one scope (a channel and a place),
// two lines of a situation are scopeCdS apart, and a channel says at most scopePer10Min lines in 10 minutes.
class Pacer
{
public:
    bool Allowed(Rule const& rule, uint64_t bot, std::string const& situation, std::string const& scope,
                 uint8_t channel, uint64_t nowS) const
    {
        if (rule.botCdS)
        {
            auto it = botLast_.find(Key(bot, situation));
            if (it != botLast_.end() && nowS < it->second + rule.botCdS)
                return false;
        }
        if (rule.scopeCdS)
        {
            auto it = scopeLast_.find(situation + "|" + scope);
            if (it != scopeLast_.end() && nowS < it->second + rule.scopeCdS)
                return false;
        }
        if (rule.scopePer10Min)
        {
            auto it = scopeCount_.find(std::to_string(channel) + "|" + scope);
            if (it != scopeCount_.end())
            {
                uint32_t recent = 0;
                for (uint64_t t : it->second)
                    if (t + 600 > nowS)
                        ++recent;
                if (recent >= rule.scopePer10Min)
                    return false;
            }
        }
        return true;
    }

    void Said(uint64_t bot, std::string const& situation, std::string const& scope, uint8_t channel, uint64_t nowS)
    {
        botLast_[Key(bot, situation)] = nowS;
        scopeLast_[situation + "|" + scope] = nowS;
        auto& q = scopeCount_[std::to_string(channel) + "|" + scope];
        q.push_back(nowS);
        while (!q.empty() && q.front() + 600 <= nowS)
            q.pop_front();
        if (++said_ % 4096 == 0)
            Prune(nowS);
    }

    size_t Size() const { return botLast_.size() + scopeLast_.size() + scopeCount_.size(); }

    // Drops what no rule can still use (a day old).
    void Prune(uint64_t nowS)
    {
        for (auto it = botLast_.begin(); it != botLast_.end();)
            it = it->second + 86400 <= nowS ? botLast_.erase(it) : std::next(it);
        for (auto it = scopeLast_.begin(); it != scopeLast_.end();)
            it = it->second + 86400 <= nowS ? scopeLast_.erase(it) : std::next(it);
        for (auto it = scopeCount_.begin(); it != scopeCount_.end();)
        {
            while (!it->second.empty() && it->second.front() + 600 <= nowS)
                it->second.pop_front();
            it = it->second.empty() ? scopeCount_.erase(it) : std::next(it);
        }
    }

private:
    static std::string Key(uint64_t bot, std::string const& situation) { return std::to_string(bot) + "|" + situation; }

    std::unordered_map<std::string, uint64_t> botLast_;
    std::unordered_map<std::string, uint64_t> scopeLast_;
    std::unordered_map<std::string, std::deque<uint64_t>> scopeCount_;
    uint64_t said_ = 0;
};

// The chance that a bot reacts: the rule's, times the soul's sociability (spec §3), times the population factor of
// the channel timers (1, 2, 4 and 5).
inline uint32_t ReactPct(Rule const& rule, int32_t sociability /* -1: no soul */, float populationFactor)
{
    float pct = static_cast<float>(rule.chancePct);
    if (sociability >= 0)
        pct *= 0.5f + static_cast<float>(sociability) / 100.0f;
    pct *= populationFactor;
    return pct <= 0.0f ? 0u : std::min<uint32_t>(100u, static_cast<uint32_t>(pct + 0.5f));
}

// "12g 50s", "75s", "8c": how players write a price (copper below a silver only).
inline std::string FormatMoney(uint64_t copper)
{
    uint64_t const g = copper / 10000, s = (copper / 100) % 100, c = copper % 100;
    if (g)
        return std::to_string(g) + "g" + (s ? " " + std::to_string(s) + "s" : "");
    if (s)
        return std::to_string(s) + "s";
    return std::to_string(c) + "c";
}

// min(1, real players / 3), floor 0.2 (spec §4).
inline float PopulationFactor(uint32_t realPlayers) { return std::max(0.2f, std::min(1.0f, realPlayers / 3.0f)); }
}  // namespace CoaLive

#endif
