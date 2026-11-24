/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_CLASS_NAMES_H
#define COA_CLASS_NAMES_H

#include <cctype>
#include <cstdint>
#include <iterator>
#include <string>

// The names of the classes, apart from the game objects, so that tests/coa_class_names.cpp can check them.

constexpr char const* COA_CLASS_NAMES[] =
{
    "Barbarian", "Witch Doctor", "Felsworn", "Witch Hunter", "Stormbringer", "Knight of Xoroth", "Guardian",
    "Templar", "Bloodmage", "Ranger", "Chronomancer", "Necromancer", "Pyromancer", "Cultist", "Starcaller",
    "Sun Cleric", "Tinker", "Venomancer", "Reaper", "Primalist", "Runemaster"
};
constexpr std::uint8_t COA_FIRST_CLASS = 12;
constexpr std::uint8_t COA_END_CLASS = COA_FIRST_CLASS + std::size(COA_CLASS_NAMES);  // one past the last (33)

// A name folded for a match: letters and digits only, lower case.
inline std::string CoaFoldedName(std::string const& text)
{
    std::string folded;
    for (char c : text)
        if (std::isalnum(static_cast<unsigned char>(c)))
            folded += char(std::tolower(static_cast<unsigned char>(c)));
    return folded;
}

// Name of one of the 21 CoA classes, or nullptr.
inline char const* CoaClassName(std::uint8_t classId)
{
    if (classId < COA_FIRST_CLASS || classId >= COA_END_CLASS)
        return nullptr;
    return COA_CLASS_NAMES[classId - COA_FIRST_CLASS];
}

// A CoA class from its name as typed in a command: case, spaces and apostrophes do not matter, and a
// beginning shared by no other class is enough ("sun" is Sun Cleric). 0 when none, or several, match.
inline std::uint8_t FindCoaClass(std::string const& name)
{
    std::string const wanted = CoaFoldedName(name);
    if (wanted.empty())
        return 0;

    std::uint8_t found = 0;
    for (std::uint8_t i = 0; i < std::size(COA_CLASS_NAMES); ++i)
    {
        std::string const candidate = CoaFoldedName(COA_CLASS_NAMES[i]);
        if (candidate == wanted)
            return COA_FIRST_CLASS + i;
        if (candidate.rfind(wanted, 0) == 0)
        {
            if (found)
                return 0;  // "witch" is Witch Doctor and Witch Hunter: say which
            found = COA_FIRST_CLASS + i;
        }
    }
    return found;
}

// 3.2.3 (the bots' addclass command): the class a name asks for, the original ones by their short name (warrior ... druid,
// dk) as before, the CoA ones as FindCoaClass reads them ("witchdoctor", "witch_doctor", "sun", "Knight-of-Xoroth").
// 0 when none, or several, match.
inline std::uint8_t AddclassClassFromName(std::string const& name)
{
    struct Original { char const* name; std::uint8_t id; };
    static constexpr Original ORIGINAL[] = { { "warrior", 1 }, { "paladin", 2 }, { "hunter", 3 }, { "rogue", 4 },
        { "priest", 5 }, { "dk", 6 }, { "shaman", 7 }, { "mage", 8 }, { "warlock", 9 }, { "druid", 11 } };
    std::string const folded = CoaFoldedName(name);
    for (Original const& original : ORIGINAL)
        if (folded == original.name)
            return original.id;
    return FindCoaClass(name);
}

#endif
