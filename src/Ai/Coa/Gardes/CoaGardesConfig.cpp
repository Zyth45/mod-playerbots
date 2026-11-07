/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaGardesConfig.h"

#include "CoaConfigAlias.h"

#include "Config.h"

#include <algorithm>

std::string CoaConfigString(std::string const& key, std::string const& alias, std::string const& fallback)
{
    auto lookup = [](std::string const& name) -> std::optional<std::string>
    {
        std::vector<std::string> const keys = sConfigMgr->GetKeysByString(name);
        if (std::find(keys.begin(), keys.end(), name) == keys.end())
            return std::nullopt;
        return sConfigMgr->GetOption<std::string>(name, "", false);
    };
    return CoaConfigWithAlias(lookup, key, alias, fallback);
}

int32 CoaConfigInt(std::string const& key, std::string const& alias, int32 fallback)
{
    std::string const value = CoaConfigString(key, alias, std::to_string(fallback));
    try
    {
        return int32(std::stol(value));
    }
    catch (...)
    {
        return fallback;
    }
}

bool CoaConfigBool(std::string const& key, std::string const& alias, bool fallback)
{
    std::string value = CoaConfigString(key, alias, fallback ? "1" : "0");
    std::transform(value.begin(), value.end(), value.begin(), ::tolower);
    if (value == "1" || value == "true" || value == "yes")
        return true;
    if (value == "0" || value == "false" || value == "no")
        return false;
    return fallback;
}
