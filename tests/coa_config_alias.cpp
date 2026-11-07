#include "CoaConfigAlias.h"

#include <cassert>
#include <map>

int main()
{
    std::map<std::string, std::string> conf;
    auto lookup = [&conf](std::string const& name) -> std::optional<std::string>
    {
        auto const itr = conf.find(name);
        return itr == conf.end() ? std::nullopt : std::optional<std::string>(itr->second);
    };
    std::string const key = "AiPlayerbot.CoaHealRepositionEnabled";
    std::string const alias = "DungeonClear.HealRepositionEnabled";

    // Neither key: the default.
    assert(CoaConfigWithAlias(lookup, key, alias, "1") == "1");
    // The new key alone: its value.
    conf[key] = "0";
    assert(CoaConfigWithAlias(lookup, key, alias, "1") == "0");
    // The player's old key wins over the new one, whatever their values.
    conf[alias] = "1";
    assert(CoaConfigWithAlias(lookup, key, alias, "0") == "1");
    conf.erase(key);
    assert(CoaConfigWithAlias(lookup, key, alias, "0") == "1");
    // An empty value is a value the player set.
    conf[alias] = "";
    assert(CoaConfigWithAlias(lookup, key, alias, "1").empty());
    // No alias: the new key, then the default.
    conf.clear();
    assert(CoaConfigWithAlias(lookup, key, "", "7") == "7");
}
