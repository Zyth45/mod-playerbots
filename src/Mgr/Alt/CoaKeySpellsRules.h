/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COAKEYSPELLSRULES_H
#define _PLAYERBOT_COAKEYSPELLSRULES_H

// AiPlayerbot.CoaKeySpells.Enable: the addon's key spells bar (SquidBots Lite, 3.1.1), for any bot of the master's
// group. "keyspells": the bot's spells with a long cooldown, "KSPELL\tit\t<spellId>\t<cdLeftMs>\t<cdTotalMs>\t<kept>"
// each, then "KSPELL\t-". "keepcd <spellId> on|off": the AI never casts a kept spell (any rank) by itself, a "cast"
// whisper still does; "KCD\tdone\t<spellId>\t<0|1>" or "KCD\terr\t<spellId>\t<unknown|off>". Kept until logout.
// Without the game objects, for the unit test.

#include <cstdint>
#include <cstdlib>
#include <string>

namespace CoaKeySpells
{
constexpr uint32_t MIN_CD_MS = 30000;  // a key spell: a base cooldown of 30 s or more
constexpr uint32_t MAX_LINES = 40;

// A spell of the list: learned and active, not passive, a base cooldown of MIN_CD_MS or more; never a profession
// spell, a mount, or a spell that creates an item.
inline bool Listed(bool passive, uint32_t cdTotalMs, bool profession, bool mount, bool createsItem)
{
    return !passive && cdTotalMs >= MIN_CD_MS && !profession && !mount && !createsItem;
}

inline std::string Line(uint32_t spellId, uint32_t cdLeftMs, uint32_t cdTotalMs, bool kept)
{
    return "KSPELL\tit\t" + std::to_string(spellId) + "\t" + std::to_string(cdLeftMs) + "\t" +
           std::to_string(cdTotalMs) + "\t" + (kept ? "1" : "0");
}

// "keepcd" arguments: "<spellId> on|off". false: not understood.
inline bool ParseKeep(std::string const& arg, uint32_t& spellId, bool& on)
{
    size_t const space = arg.find(' ');
    if (space == std::string::npos)
        return false;
    std::string const id = arg.substr(0, space);
    std::string mode = arg.substr(space + 1);
    while (!mode.empty() && mode.back() == ' ')
        mode.pop_back();
    if (id.empty() || id.find_first_not_of("0123456789") != std::string::npos || id.size() > 9)
        return false;
    spellId = uint32_t(std::strtoul(id.c_str(), nullptr, 10));
    if (!spellId || (mode != "on" && mode != "off"))
        return false;
    on = mode == "on";
    return true;
}
}  // namespace CoaKeySpells

#endif
