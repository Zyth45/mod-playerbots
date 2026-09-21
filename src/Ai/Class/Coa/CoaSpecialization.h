/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COASPECIALIZATION_H
#define _PLAYERBOT_COASPECIALIZATION_H

#include "Define.h"

#include <string>

class Player;

enum class CoaRole : uint8
{
    Dps,
    Tank,
    Heal
};

// How a CoA character fights, from the ascensionsidekick.com role of its specialization.
enum class CoaStyle : uint8
{
    Melee,   // strikes in melee range
    Ranged,  // physical damage from range: bows, guns, thrown weapons
    Caster   // spells from range
};

// Primary stats of a CoA specialization, as a mask.
enum CoaStat : uint8
{
    COA_STAT_STRENGTH  = 0x01,
    COA_STAT_AGILITY   = 0x02,
    COA_STAT_INTELLECT = 0x04,
    COA_STAT_SPIRIT    = 0x08,
    COA_STAT_STAMINA   = 0x10
};

// Armor a CoA class is proficient with. CoA grants every armor proficiency of a class at
// character creation (acore_world.playercreateinfo_spell_custom, required level 0), so this
// never depends on the character's level, unlike the level 40 step of the WotLK classes.
struct CoaArmorProficiency
{
    uint8 heaviestArmor;  // ITEM_SUBCLASS_ARMOR_*
    bool usesShield;
};

// Armor proficiencies of one of the 21 CoA classes, or nullptr for any other class.
CoaArmorProficiency const* GetCoaArmorProficiency(uint8 playerClass);

// Role of a Conquest of Azeroth character, from its active specialization. Dps when it has none.
CoaRole GetCoaRole(Player const* player);

// Fighting style and primary stats of a CoA character's specialization. Before it has one
// (under level 10), those shared by most specializations of its class.
CoaStyle GetCoaStyle(Player const* player);
uint8 GetCoaPrimaryStats(Player const* player);

// Gives a random bot of a CoA class a specialization once it reaches level 10, the way a
// player picks one in the client. Returns true when a specialization was just chosen.
bool EnsureCoaSpecialization(Player* bot);

// Spends a random bot's Character Advancement points the way a player of its specialization
// would: one ability essence at every even level from 10 (class tree), one talent essence at every
// odd level from 11 (specialization tree), in the order of the ascensionsidekick.com level build.
// Returns the number of entries raised.
uint32 ApplyCoaTalents(Player* bot);

// Brings the nearest free random bot able to fill `role` into the master's group: raised to
// the master's level, given a specialization of that role, teleported next to the master.
// Returns false with the reason in `message` when no bot can be recruited.
// Name of one of the 21 CoA classes, or nullptr.
char const* CoaClassName(uint8 classId);

// A CoA class from its name as typed in a command: case, spaces and apostrophes do not matter, and a
// beginning shared by no other class is enough ("sun" is Sun Cleric). 0 when none, or several, match.
uint8 FindCoaClass(std::string const& name);

// Recruits a free random bot for the role, of that class when classId is not 0.
bool RecruitCoaBot(Player* master, CoaRole role, std::string& message, uint8 classId = 0);

#endif
