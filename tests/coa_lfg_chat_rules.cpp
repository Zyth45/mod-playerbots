#include "CoaLfgChatRules.h"

#include <cassert>

namespace
{
std::vector<CoaLfgAsk> Asks(std::string const& message, bool requireBotWord = true)
{
    return CoaLfgAsks(CoaParseLfgRequest(message), requireBotWord);
}

constexpr std::uint8_t WitchDoctor = 13;
constexpr std::uint8_t Guardian = 18;
constexpr std::uint8_t SunCleric = 27;
constexpr std::uint8_t Tinker = 28;
constexpr std::uint8_t KnightOfXoroth = 17;
constexpr std::uint8_t WitchHunter = 15;
constexpr std::uint8_t Runemaster = 32;
}  // namespace

int main()
{
    // Roles as before.
    assert((Asks("lfg bot heal") == std::vector<CoaLfgAsk>{{CoaLfgRoleHeal, 0}}));
    assert((Asks("LFG bot tank, dps") == std::vector<CoaLfgAsk>{{CoaLfgRoleTank, 0}, {CoaLfgRoleDps, 0}}));
    assert((Asks("lfg bot") ==
            std::vector<CoaLfgAsk>{{CoaLfgRoleTank, 0}, {CoaLfgRoleHeal, 0}, {CoaLfgRoleDps, 0}}));
    assert(Asks("lfg heal").empty());
    assert((Asks("lfg heal", false) == std::vector<CoaLfgAsk>{{CoaLfgRoleHeal, 0}}));
    assert(Asks("bot heal tinker").empty());
    assert(Asks("hello everyone").empty());

    // A class takes the role after it, else the one before it.
    assert((Asks("lfg bot tinker heal") == std::vector<CoaLfgAsk>{{CoaLfgRoleHeal, Tinker}}));
    assert((Asks("lf bot guardian tank") == std::vector<CoaLfgAsk>{{CoaLfgRoleTank, Guardian}}));
    assert((Asks("lfg bot heal tinker") == std::vector<CoaLfgAsk>{{CoaLfgRoleHeal, Tinker}}));
    assert((Asks("lfm bot witch doctor") == std::vector<CoaLfgAsk>{{CoaLfgAnyRole, WitchDoctor}}));
    assert((Asks("lfm bot Witch Doctors!") == std::vector<CoaLfgAsk>{{CoaLfgAnyRole, WitchDoctor}}));
    assert((Asks("lfg bot sun cleric heal, knight of xoroth tank") ==
            std::vector<CoaLfgAsk>{{CoaLfgRoleHeal, SunCleric}, {CoaLfgRoleTank, KnightOfXoroth}}));
    assert((Asks("lfg bot tinker heal guardian tank dps") ==
            std::vector<CoaLfgAsk>{{CoaLfgRoleHeal, Tinker}, {CoaLfgRoleTank, Guardian}, {CoaLfgRoleDps, 0}}));
    assert((Asks("lfg bot witch hunter dps runemaster") ==
            std::vector<CoaLfgAsk>{{CoaLfgRoleDps, WitchHunter}, {CoaLfgAnyRole, Runemaster}}));
    // Twice the same ask counts once.
    assert((Asks("lfg bot tinker heal tinker heal") == std::vector<CoaLfgAsk>{{CoaLfgRoleHeal, Tinker}}));

    // No short forms or half names: they are chat words.
    assert((Asks("lfg bot dk tank") == std::vector<CoaLfgAsk>{{CoaLfgRoleTank, 0}}));
    assert((Asks("lfg bot wd heal") == std::vector<CoaLfgAsk>{{CoaLfgRoleHeal, 0}}));
    assert((Asks("lfg bot sun heal") == std::vector<CoaLfgAsk>{{CoaLfgRoleHeal, 0}}));
    assert((Asks("lfg bot witch heal") == std::vector<CoaLfgAsk>{{CoaLfgRoleHeal, 0}}));
    assert((Asks("lfg bot knight tank") == std::vector<CoaLfgAsk>{{CoaLfgRoleTank, 0}}));
    assert((Asks("lfg bot doctor") ==
            std::vector<CoaLfgAsk>{{CoaLfgRoleTank, 0}, {CoaLfgRoleHeal, 0}, {CoaLfgRoleDps, 0}}));

    // Every class is known by its full name.
    for (std::size_t i = 0; i < std::size(CoaLfgClassNames); ++i)
    {
        std::vector<CoaLfgAsk> const got = Asks(std::string("lfg bot ") + CoaLfgClassNames[i] + " dps");
        assert(got.size() == 1 && got[0].classId == CoaLfgFirstClass + i && got[0].role == CoaLfgRoleDps);
    }

    // Up to two bots answer a class request.
    static_assert(CoaLfgClassOffers == 2);

    // The wait line names the class (the addon greys only that class).
    assert(CoaLfgClassWaitText("Tinker", 42) == "You can ask for a Tinker again in 42 s: your request is answered by role.");
    assert(CoaLfgClassWaitText("Knight of Xoroth", 300) ==
           "You can ask for a Knight of Xoroth again in 300 s: your request is answered by role.");

    // The class wait (kept per account and class by the caller).
    assert(CoaLfgClassAskAllowed(0, 1000, 300));
    assert(!CoaLfgClassAskAllowed(1000, 1299, 300));
    assert(CoaLfgClassAskAllowed(1000, 1300, 300));
    assert(CoaLfgClassAskAllowed(1000, 1001, 0));
    return 0;
}
