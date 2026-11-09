/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA World: rules of the guild charters (no game type, tested in tests/coa_world_charter_rules.cpp).

#ifndef PLAYERBOTS_COAWORLDCHARTERRULES_H
#define PLAYERBOTS_COAWORLDCHARTERRULES_H

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>

namespace CoaWorldCharterRules
{
// A stranger's guild charter is the only request whose security check is relaxed, and only for a random bot of
// its own (no real-player master: not an alt, not a player's bot) that has no guild nor guild invitation.
// guildCharter: the petition is a guild charter (not an arena team's).
inline bool StrangerMaySign(bool guildCharter, bool randomBot, bool hasRealMaster, bool inGuild, bool invited)
{
    return guildCharter && randomBot && !hasRealMaster && !inGuild && !invited;
}

// Share of random bots without a guild, in percent (100 when there is none).
inline uint32_t UnguildedPct(uint32_t unguilded, uint32_t total) { return total ? unguilded * 100 / total : 100; }

// A bot may found or join a bot guild only while the bots left without a guild stay above the floor, counting
// those it takes (a charter takes the founder and its signers).
inline bool AboveFloor(uint32_t unguilded, uint32_t total, uint32_t taking, uint32_t floorPct)
{
    if (!floorPct)
        return true;
    if (unguilded < taking)
        return false;
    return UnguildedPct(unguilded - taking, total) >= floorPct;
}

// A player asks for signatures aloud: "sign", "charter" or "signature" as a word.
inline bool AsksSignatures(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    for (char& c : text)
        if (!std::isalnum(static_cast<unsigned char>(c)))
            c = ' ';
    text = " " + text + " ";
    for (char const* word : { " sign ", " signs ", " charter ", " signature ", " signatures ", " signer ", " charte " })
        if (text.find(word) != std::string::npos)
            return true;
    return false;
}
}  // namespace CoaWorldCharterRules

#endif
