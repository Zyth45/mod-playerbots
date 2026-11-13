// CoA Economy: CoaEconomyConfDefaults.inc holds exactly the .dist values (apps/coa_economy/gen_conf_defaults.py).
#include <cassert>
#include <cstdio>
#include <fstream>
#include <map>
#include <regex>
#include <string>

static std::map<std::string, std::string> const table = {
#include "Ai/Coa/Economy/CoaEconomyConfDefaults.inc"
};

int main()
{
    std::string root = __FILE__;
    root = root.substr(0, root.find_last_of("/\\"));
    root = root.substr(0, root.find_last_of("/\\"));
    std::regex const key(R"(^((?:AiPlayerbot\.CoaEconomy|AuctionHouseBot)\.[A-Za-z0-9_.]+)\s*=\s*(.*?)\s*$)");
    std::map<std::string, std::string> dist;
    for (char const* file : {"/conf/playerbots.conf.dist", "/conf/mod_ahbot.conf.dist"})
    {
        std::ifstream in(root + file);
        assert(in && "dist file");
        std::string line;
        std::smatch m;
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (std::regex_match(line, m, key))
            {
                std::string value = m[2];
                if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
                    value = value.substr(1, value.size() - 2);
                dist[m[1]] = value;
            }
        }
    }
    for (auto const& [k, v] : dist)
    {
        auto found = table.find(k);
        if (found == table.end() || found->second != v)
            std::printf("stale: %s (run apps/coa_economy/gen_conf_defaults.py)\n", k.c_str());
        assert(found != table.end() && found->second == v);
    }
    assert(dist.size() == table.size());
    // The 3.0.1 economy, the default of every realm (an update is seen by all).
    assert(table.at("AiPlayerbot.CoaEconomy.Artisans.LearnRareRecipes.Enable") == "1");
    assert(table.at("AiPlayerbot.CoaEconomy.Artisans.MaterialsOnDelivery") == "1");
    assert(table.at("AiPlayerbot.CoaEconomy.BotStartGoldPercent") == "0");
    assert(table.at("AiPlayerbot.CoaEconomy.PriceScalePercent") == "60");
    assert(table.at("AiPlayerbot.CoaEconomy.LaunchCurve.LaunchPremiumPercent") == "0");
    assert(table.at("AiPlayerbot.CoaEconomy.LaunchCurve.Enable") == "1");
    assert(table.at("AiPlayerbot.CoaEconomy.BotLevelGold") ==
           "10:5000,15:10000,20:20000,25:30000,30:50000,35:70000,40:100000,45:120000,50:150000,55:200000,60:250000,"
           "65:300000,70:350000,75:400000,80:500000");
    std::printf("coa_economy_conf_defaults PASS (%zu keys)\n", table.size());
    return 0;
}
