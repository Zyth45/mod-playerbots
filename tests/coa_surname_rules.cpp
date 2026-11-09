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

    auto const all = [](std::string const&) { return true; };
    auto const apply = [](std::vector<CoaNameRow>& rows, std::vector<CoaRename> const& renames)
    {
        for (CoaRename const& r : renames)
            for (CoaNameRow& row : rows)
                if (row.guid == r.guid)
                {
                    assert(row.name == r.from);
                    row.name = r.to;
                }
    };
    auto const allUnique = [](std::vector<CoaNameRow> const& rows)
    {
        std::set<std::string> keys;
        for (CoaNameRow const& row : rows)
            if (!keys.insert(CoaNameKey(row.name)).second)
                return false;
        return true;
    };
    std::vector<std::string> const one{"Ashford"};

    // 1. A full name stays unique: two bots of the same first name and the same family name, only one gets it.
    {
        std::vector<CoaNameRow> rows{{10, "Kegarink Bot", true}, {11, "Kegarink", true}, {12, "Doran Bot", true}};
        uint32_t kept = 0;
        std::vector<CoaRename> renames = CoaPlanBotRenames(rows, 2, one, all, &kept);
        assert(renames.size() == 2 && kept == 1);
        apply(rows, renames);
        assert(allUnique(rows));
        assert(rows[0].name == "Kegarink Ashford" && rows[1].name == "Kegarink" && rows[2].name == "Doran Ashford");
        // A name the realm refuses is never given.
        rows = {{10, "Kegarink Bot", true}};
        renames = CoaPlanBotRenames(rows, 2, one, [](std::string const& n) { return n != "Kegarink Ashford"; }, &kept);
        assert(renames.empty() && kept == 1);
    }

    // 2. A real player (or an alt) already holds the name, in any case: the bot keeps its own, the player is never
    // renamed.
    {
        std::vector<CoaNameRow> rows{{1, "Kegarink ashford", false}, {10, "Kegarink Bot", true}, {2, "Doran Bot", false}};
        uint32_t kept = 0;
        std::vector<CoaRename> renames = CoaPlanBotRenames(rows, 2, one, all, &kept);
        assert(renames.empty() && kept == 1);
        renames = CoaPlanBotRenames(rows, 0, one, all, &kept);
        assert(renames.size() == 1 && renames[0].guid == 10 && renames[0].to == "Kegarink");
        renames = CoaPlanBotRenames(rows, 1, one, all, &kept);
        assert(renames.empty() && kept == 0);  // "Doran Bot" is a player: untouched
    }

    // 3. 1 -> 2 -> 1 gives every bot its "First Bot" name back, same guids (friends, guild, mail and arena teams
    // hold guids, not names); a "First Bot" taken meanwhile by a player keeps the bot on its family name.
    {
        std::vector<CoaNameRow> start;
        for (uint64_t guid = 100; guid < 400; ++guid)
            start.push_back({guid, "Name" + std::string(1, static_cast<char>('a' + guid % 26)) +
                                       std::string(1, static_cast<char>('a' + (guid / 26) % 26)) + " Bot", true});
        start.push_back({1, "Hero", false});
        std::vector<CoaNameRow> rows = start;
        apply(rows, CoaPlanBotRenames(rows, 2, defaults, all));
        assert(allUnique(rows));
        for (size_t i = 0; i + 1 < rows.size(); ++i)
            assert(rows[i].name == CoaBotFirstName(start[i].name) + " " + CoaSurnameFor(rows[i].guid, defaults));
        apply(rows, CoaPlanBotRenames(rows, 1, defaults, all));
        for (size_t i = 0; i < rows.size(); ++i)
            assert(rows[i].guid == start[i].guid && rows[i].name == start[i].name);

        rows = {{10, "Kegarink Ashford", true}, {1, "Kegarink Bot", false}};
        uint32_t kept = 0;
        assert(CoaPlanBotRenames(rows, 1, one, all, &kept).empty() && kept == 1);
    }
    // A bot's full name in a GM command.
    {
        auto resolve = [](std::string const& n) -> uint64_t { return n == "Tianor Bot" ? 4242 : 0; };
        assert(CoaQuotedNamesToGuids("guild invite \"Tianor Bot\" \"Les Lions\"", resolve) == "guild invite 4242 \"Les Lions\"");
        assert(CoaQuotedNamesToGuids("guild invite \"Tianor\" \"Les Lions\"", resolve).empty());
        assert(CoaQuotedNamesToGuids("guild create \"Les Lions\"", resolve).empty());   // a space, but nobody's name
        assert(CoaQuotedNamesToGuids("tele name \"Tianor Bot\" Stormwind", resolve) == "tele name 4242 Stormwind");
        assert(CoaQuotedNamesToGuids("announce \"unclosed", resolve).empty());
    }
    return 0;
}
