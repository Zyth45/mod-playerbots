#include "CoaSurnameRules.h"

#include <cassert>
#include <map>

int main()
{
    assert(CoaSurnameModeOf(0) == COA_SURNAME_NONE);
    assert(CoaSurnameModeOf(1) == COA_SURNAME_BOT);
    assert(CoaSurnameModeOf(2) == COA_SURNAME_FAMILY);
    assert(CoaSurnameModeOf(3) == COA_SURNAME_BOT);
    assert(CoaSurnameModeOf(-1) == COA_SURNAME_BOT);

    // Every default name is one the realm accepts, and is there once.
    std::vector<std::string> const& defaults = CoaDefaultSurnames();
    assert(defaults.size() == 64);
    std::set<std::string> unique;
    for (std::string name : defaults)
    {
        std::string checked = name;
        assert(CoaSurnameWord(checked) && checked == name);
        assert(unique.insert(name).second);
    }

    // The list of the launcher: quoted, comma separated, spaces dropped.
    std::vector<std::string> rejected;
    std::vector<std::string> list = CoaParseSurnameList("\"Ashford, Brightwood\"", &rejected);
    assert(list.size() == 2 && list[0] == "Ashford" && list[1] == "Brightwood" && rejected.empty());
    list = CoaParseSurnameList(" ashford ,ASHFORD,, Iron field, Toolongsurname, Baaad, X , Stormhand", &rejected);
    assert(list.size() == 2 && list[0] == "Ashford" && list[1] == "Stormhand");
    assert(rejected.size() == 4);  // "Iron field", "Toolongsurname", "Baaad", "X"
    assert(CoaParseSurnameList("") == defaults);
    assert(CoaParseSurnameList("\"\"") == defaults);
    assert(CoaParseSurnameList(" , 123") == defaults);

    // Same guid, same name; the names spread over the list.
    assert(CoaSurnameFor(4242, defaults) == CoaSurnameFor(4242, defaults));
    std::map<std::string, int> spread;
    for (uint64_t guid = 1; guid <= 6400; ++guid)
        ++spread[CoaSurnameFor(guid, defaults)];
    assert(spread.size() == 64);
    for (auto const& [name, count] : spread)
        assert(count > 50 && count < 150);

    // The wanted name in each mode, from any current name.
    std::vector<std::string> const two{"Ashford", "Brightwood"};
    for (std::string current : {"Kegarink", "Kegarink Bot", "Kegarink Ashford", "Kegarink Brightwood"})
    {
        assert(CoaWantedBotName(current, 0, 7, two) == "Kegarink");
        assert(CoaWantedBotName(current, 1, 7, two) == "Kegarink Bot");
        assert(CoaWantedBotName(current, 2, 7, two) == "Kegarink " + CoaSurnameFor(7, two));
    }
    assert(CoaWantedBotName("Kegarink", 2, 7, {}) == "Kegarink");
    return 0;
}
