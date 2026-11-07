/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_ESCAPE_LEAP_H
#define COA_ESCAPE_LEAP_H

#include <string_view>

// The escape leap ban, from Dungeon Clear (DcEscapeLeap.h): mage `blink back` (action "blink") and hunter `disengage`
// throw the caster a fixed distance straight away from its target with no look at what is behind it. They answer
// "enemy too close", which is permanently true for the ranged in a group fight: the tank drags the pack onto the party.
// Out in the world that is a kite; in a dungeon or raid it lands the bot in the next pack (Karazhan's banquet hall:
// six elites pulled, thirty-six on the party). No player uses them that way in group content.
namespace CoaEscapeLeap
{
    inline bool IsBanned(std::string_view actionName)
    {
        return actionName == "blink" || actionName == "blink back" || actionName == "disengage";
    }

    // The ban applies to every member of a group with a reference tank inside a dungeon or raid; on the combat engine
    // a raid encounter's stand-down (Dungeon Clear run) hands the fight to the raid strategy, which rules on its own
    // leaps.
    struct Gate
    {
        bool hasTank = false;
        bool inInstance = false;
        bool combatEngine = false;
        bool raidStandDown = false;
    };

    constexpr bool Bans(bool banned, Gate const& g)
    {
        return banned && g.hasTank && g.inInstance && !(g.combatEngine && g.raidStandDown);
    }
}

#endif
