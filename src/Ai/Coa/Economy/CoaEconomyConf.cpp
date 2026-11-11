/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaEconomyConf.h"

#include <string_view>
#include <unordered_map>

namespace CoaEconomy::Conf
{

namespace
{
    std::unordered_map<std::string_view, char const*> const& Table()
    {
        static std::unordered_map<std::string_view, char const*> const table = {
#include "CoaEconomyConfDefaults.inc"
        };
        return table;
    }

    constexpr char const* Absent = "\x01coa-absent\x01";
}

char const* DistValue(std::string const& key)
{
    auto found = Table().find(key);
    return found == Table().end() ? nullptr : found->second;
}

bool IsSet(std::string const& key)
{
    return sConfigMgr->GetOption<std::string>(key, Absent, false) != Absent;
}

} // namespace CoaEconomy::Conf
