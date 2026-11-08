/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAPETRULES_H
#define PLAYERBOTS_COAPETRULES_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// The Pets tab of the SquidBots Lite addon (protocol PETS-PROTOCOLE-3.0): an alt's utility companions, by category,
// with a favourite each. Pure rules: the table, the command, the stored choice and the addon lines.
namespace CoaPet
{
enum Category : uint8_t
{
    CAT_LOOT = 0,
    CAT_QUEST,
    CAT_CLASS,
    CAT_CRAFT,
    CAT_COUNT,
    CAT_NONE = CAT_COUNT,
};

inline char const* CategoryName(Category c)
{
    static char const* const names[] = {"loot", "quest", "class", "craft"};
    return c < CAT_COUNT ? names[c] : "";
}

inline Category CategoryOf(std::string const& name)
{
    for (uint8_t c = 0; c < CAT_COUNT; ++c)
        if (name == CategoryName(Category(c)))
            return Category(c);
    return CAT_NONE;
}

struct Companion
{
    uint32_t spellId;
    uint32_t entry;
    Category category;
    char const* name;  // when the creature has none
};

// The companions of the protocol, checked against the CoA data (coa-local: spells, items, our world DB).
inline std::vector<Companion> const& Companions()
{
    static std::vector<Companion> const all = {
        {9930889, 44022, CAT_LOOT, "Lootbot 3000"},
        {83086, 79026, CAT_LOOT, "Kleptomanic Demon"},
        {83050, 79025, CAT_QUEST, "Wondrous Wisdomball"},
        {979602, 75115, CAT_CLASS, "Book of Ascension"},
        {499992, 499992, CAT_CLASS, "Book of Ascension"},
        {499908, 75137, CAT_CLASS, "Bloody Book of Ascension"},
        {105307, 108587, CAT_CLASS, "Book of Conquest"},
        {750750, 57500, CAT_CRAFT, "Book of Artisans"},
    };
    return all;
}

inline Companion const* BySpell(uint32_t spellId)
{
    for (Companion const& c : Companions())
        if (c.spellId == spellId)
            return &c;
    return nullptr;
}

inline Companion const* ByEntry(uint32_t entry)
{
    for (Companion const& c : Companions())
        if (c.entry == entry)
            return &c;
    return nullptr;
}

// The companion to bring out for a category: the favourite when known, else the first known of the category.
template <class Knows>
uint32_t Choose(Category category, uint32_t favourite, Knows knows)
{
    if (favourite)
        if (Companion const* c = BySpell(favourite); c && c->category == category && knows(favourite))
            return favourite;
    for (Companion const& c : Companions())
        if (c.category == category && knows(c.spellId))
            return c.spellId;
    return 0;
}

// ---------------------------------------------------------------- command

enum class Verb
{
    None,      // not a Pets verb: the hunter "pet" command keeps it
    List,
    Set,
    Use,
    Dismiss,
    Auto,
};

struct Command
{
    Verb verb = Verb::None;
    Category category = CAT_NONE;
    uint32_t spellId = 0;   // set: 0 clears the favourite; use: a companion by spell
    bool on = true;         // auto
    bool valid = false;     // the arguments made sense
};

inline std::vector<std::string> Words(std::string const& text)
{
    std::vector<std::string> out;
    size_t i = 0;
    while (i < text.size())
    {
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t'))
            ++i;
        size_t j = i;
        while (j < text.size() && text[j] != ' ' && text[j] != '\t')
            ++j;
        if (j > i)
            out.push_back(text.substr(i, j - i));
        i = j;
    }
    return out;
}

inline bool Number(std::string const& s, uint32_t& out)
{
    if (s.empty() || s.size() > 9 || s.find_first_not_of("0123456789") != std::string::npos)
        return false;
    out = static_cast<uint32_t>(std::stoul(s));
    return true;
}

// "list", "set <category> <spellId|0>", "use <category|spellId>", "dismiss", "auto on|off" (the param of "pet").
inline Command Parse(std::string const& param)
{
    Command cmd;
    std::vector<std::string> const w = Words(param);
    if (w.empty())
        return cmd;
    if (w[0] == "list")
    {
        cmd.verb = Verb::List;
        cmd.valid = w.size() == 1;
    }
    else if (w[0] == "set")
    {
        cmd.verb = Verb::Set;
        cmd.valid = w.size() == 3 && (cmd.category = CategoryOf(w[1])) != CAT_NONE && Number(w[2], cmd.spellId) &&
                    (cmd.spellId == 0 || (BySpell(cmd.spellId) && BySpell(cmd.spellId)->category == cmd.category));
    }
    else if (w[0] == "use")
    {
        cmd.verb = Verb::Use;
        if (w.size() == 2)
        {
            if ((cmd.category = CategoryOf(w[1])) != CAT_NONE)
                cmd.valid = true;
            else if (Number(w[1], cmd.spellId) && BySpell(cmd.spellId))
            {
                cmd.category = BySpell(cmd.spellId)->category;
                cmd.valid = true;
            }
        }
    }
    else if (w[0] == "dismiss")
    {
        cmd.verb = Verb::Dismiss;
        cmd.valid = w.size() == 1;
    }
    else if (w[0] == "auto")
    {
        cmd.verb = Verb::Auto;
        cmd.valid = w.size() == 2 && (w[1] == "on" || w[1] == "off");
        cmd.on = w.size() == 2 && w[1] == "on";
    }
    return cmd;
}

// ---------------------------------------------------------------- stored choice

// The favourites of quest, class and craft, and the auto flag (the loot favourite is the loot pet's own, 2.9.5).
struct Choice
{
    std::array<uint32_t, CAT_COUNT> favourite{};
    bool autoOn = true;
};

// "quest,class,craft,auto": "83050,0,750750,1".
inline std::string Store(Choice const& c)
{
    return std::to_string(c.favourite[CAT_QUEST]) + "," + std::to_string(c.favourite[CAT_CLASS]) + "," +
           std::to_string(c.favourite[CAT_CRAFT]) + "," + (c.autoOn ? "1" : "0");
}

inline Choice Load(std::string const& data)
{
    Choice c;
    std::vector<uint32_t> v;
    size_t start = 0;
    while (start <= data.size())
    {
        size_t end = data.find(',', start);
        if (end == std::string::npos)
            end = data.size();
        uint32_t n = 0;
        if (!Number(data.substr(start, end - start), n))
            return Choice();
        v.push_back(n);
        start = end + 1;
    }
    if (v.size() != 4)
        return Choice();
    c.favourite[CAT_QUEST] = v[0];
    c.favourite[CAT_CLASS] = v[1];
    c.favourite[CAT_CRAFT] = v[2];
    c.autoOn = v[3] != 0;
    return c;
}

// ---------------------------------------------------------------- addon lines (prefix PET, tab-separated)

inline std::string PetLine(Companion const& c, bool known, std::string const& name)
{
    return "PET\tpet\t" + std::to_string(c.spellId) + "\t" + std::to_string(c.entry) + "\t" + CategoryName(c.category) +
           "\t" + (known ? "1" : "0") + "\t" + name;
}
inline std::string FavLine(Category c, uint32_t spellId)
{
    return std::string("PET\tfav\t") + CategoryName(c) + "\t" + std::to_string(spellId);
}
inline std::string OutLine(uint32_t spellId) { return "PET\tout\t" + std::to_string(spellId); }
inline std::string AutoLine(bool on) { return std::string("PET\tauto\t") + (on ? "1" : "0"); }
inline std::string ErrLine(std::string const& reason) { return "PET\terr\t" + reason; }
inline std::string EndLine() { return "PET\t-"; }

// Why a companion cannot come out now, "" when it can (the loot pet's reasons, 2.9.5).
inline std::string Refusal(bool known, bool alive, bool inCombat, bool mounted, bool lootInRaid)
{
    if (!known)
        return "unknown";
    if (!alive)
        return "dead";
    if (inCombat)
        return "combat";
    if (mounted)
        return "mounted";
    if (lootInRaid)
        return "raid";
    return "";
}
}  // namespace CoaPet

#endif
