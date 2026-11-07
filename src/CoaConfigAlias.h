/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_CONFIG_ALIAS_H
#define COA_CONFIG_ALIAS_H

#include <optional>
#include <string>

// A setting that moved from Dungeon Clear to the module (combat guards, 07/11): AiPlayerbot.<X> is the new key, and
// the player's DungeonClear.<Y> is read FIRST when present, so that an update never changes a value the player set
// (the player's configuration is never written: the new key is only added to the .dist at its default). Apart from
// the game objects so that tests/coa_config_alias.cpp can check it: `lookup(name)` is the raw value of a key the
// configuration holds, or nothing.
template <class Lookup>
std::string CoaConfigWithAlias(Lookup const& lookup, std::string const& key, std::string const& alias,
                               std::string const& fallback)
{
    if (!alias.empty())
        if (std::optional<std::string> const old = lookup(alias))
            return *old;
    if (std::optional<std::string> const value = lookup(key))
        return *value;
    return fallback;
}

#endif
