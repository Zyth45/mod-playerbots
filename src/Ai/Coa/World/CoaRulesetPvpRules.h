/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_RULESET_PVP_RULES_H
#define COA_RULESET_PVP_RULES_H

// The PvP flag of a bot under a CoA ruleset, apart from the game objects, so that tests/coa_ruleset_pvp_rules.cpp can
// check it. Same rule as the core (src/server/coa/AscensionRulesets.cpp, InPvPRuleset and SyncRulesetPvP): High Risk,
// or War Mode without the PvE marker, keeps the PvP flag on; the PvE ruleset lets it go.

// Whether the auras put the character under a PvP ruleset.
inline bool CoaInPvpRuleset(bool highRisk, bool warMode, bool pveMarker)
{
    return highRisk || (warMode && !pveMarker);
}

// The PvP state a random bot gets back when it is reset (refresh after a death, random teleport): the realm's
// default, but never off for a bot under a PvP ruleset (a High Risk bot on a PvE realm lost its flag there).
inline bool CoaBotPvpAfterReset(bool pvpRealm, bool inPvpRuleset)
{
    return pvpRealm || inPvpRuleset;
}

// What to do with the PLAYER_FLAGS_IN_PVP flag: +1 set it, -1 clear it, 0 leave it. Driven by the auras the bot has,
// not by the ruleset it should get: a change the core refused leaves the flag matching what the bot really is.
inline int CoaRulesetPvpFlagChange(bool inPvpRuleset, bool pveMarker, bool hasFlag)
{
    if (inPvpRuleset)
        return hasFlag ? 0 : 1;
    return pveMarker && hasFlag ? -1 : 0;
}

#endif
