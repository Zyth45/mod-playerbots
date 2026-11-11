/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_ECONOMY_CONF_H
#define COA_ECONOMY_CONF_H

#include "Config.h"

#include <cstdlib>
#include <string>
#include <type_traits>

// CoA Economy: options read with the .dist value as their default (Pilote, 11/11). An install updated to 3.0 keeps
// its old .conf, where the 3.0 keys are missing: such a key takes the value of playerbots.conf.dist or
// mod_ahbot.conf.dist (CoaEconomyConfDefaults.inc, generated), never a code default that could differ from it.
namespace CoaEconomy::Conf
{
    // The .dist value of a key, nullptr when no .dist has it.
    char const* DistValue(std::string const& key);
    // In the loaded config files.
    bool IsSet(std::string const& key);

    template <class T>
    T Parse(char const* text, T fallback)
    {
        if constexpr (std::is_same_v<T, std::string>)
            return text;
        else if constexpr (std::is_same_v<T, bool>)
        {
            std::string const v = text;
            return v == "1" || v == "true" || v == "TRUE" || v == "True" || v == "yes";
        }
        else
        {
            char* end = nullptr;
            T value;
            if constexpr (std::is_floating_point_v<T>)
                value = T(std::strtod(text, &end));
            else if constexpr (std::is_signed_v<T>)
                value = T(std::strtoll(text, &end, 10));
            else
                value = T(std::strtoull(text, &end, 10));
            return end == text ? fallback : value;
        }
    }

    // sConfigMgr->GetOption, except that a key missing from the config files takes its .dist value.
    template <class T>
    T Get(std::string const& key, T codeDefault, bool showLogs = true)
    {
        if (IsSet(key))
            return sConfigMgr->GetOption<T>(key, codeDefault, showLogs);
        if (char const* dist = DistValue(key))
            return Parse<T>(dist, codeDefault);
        return sConfigMgr->GetOption<T>(key, codeDefault, showLogs);
    }
}

#endif
