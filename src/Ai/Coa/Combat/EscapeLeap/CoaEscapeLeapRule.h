/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COA_ESCAPE_LEAP_RULE_H
#define PLAYERBOTS_COA_ESCAPE_LEAP_RULE_H

#include <string>

// No escape leap in a group fight (CoA Combat, guard A1).
//
// The mage's Blink and the hunter's Disengage throw the caster about 20 yd straight back, without looking at what is
// there, as soon as an enemy gets close. For a ranged member of a dungeon group that is true in every fight, and the
// bot lands in the next pack. In a dungeon or a raid, with a tank leading the group, the two actions never fire.
//
// This header holds the decision only, with no game types, so the unit test can build it alone.
namespace CoaEscapeLeapRule
{
    // The action names the stock mage and hunter strategies use (the mage's action answers to both "blink" and its
    // registry key "blink back"). Matched exactly.
    inline bool IsEscapeLeap(std::string const& actionName)
    {
        return actionName == "blink" || actionName == "blink back" || actionName == "disengage";
    }

    struct Situation
    {
        bool groupHasTank = false;           // the bot's group has a living tank to fight behind
        bool inDungeonOrRaid = false;        // dungeon and raid maps only: the open world keeps its leaps
        bool combatEngine = false;           // the decision is made for the combat engine
        bool raidEncounterInProgress = false; // a raid boss fight: its raid strategy decides the movement
    };

    // True when an escape leap must not fire. During a raid encounter the ban is lifted on the combat engine only.
    inline bool Forbids(Situation const& s)
    {
        if (!s.groupHasTank || !s.inDungeonOrRaid)
            return false;

        return !(s.combatEngine && s.raidEncounterInProgress);
    }
}

#endif
