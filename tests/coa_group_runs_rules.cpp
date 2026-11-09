#include "CoaGroupRunsRules.h"

#include <cassert>

using namespace CoaWorldFile;

int main()
{
    RunBook book;
    Run seen;
    seen.instance = 7;
    seen.map = 36;
    seen.name = "The Deadmines";
    seen.mix.Add(Kind::Player);
    seen.mix.Add(Kind::Bot);
    seen.leader = "Jaina";
    seen.bossesTotal = 6;
    book.Seen(seen, 1000);
    book.Close(1000, 0);
    assert(book.Running() == 1);
    std::string j = book.Json();
    assert(j.find("\"running\":[{\"instance\":7,\"map\":36,\"dungeon\":null,\"dungeonName\":\"The Deadmines\",\"kind\":"
                  "\"dungeon\",\"difficulty\":0,\"players\":1,\"alts\":0,\"bots\":1,\"leader\":\"Jaina\","
                  "\"startedAt\":1000,\"bossesKilled\":0,\"bossesTotal\":6,\"wipes\":0,\"dc\":null}]") !=
           std::string::npos);

    book.Kill(7, 639, false, 1100);
    book.Kill(7, 639, false, 1101);  // the same boss twice: one
    book.Wipe(7);
    book.Kill(99, 1, false, 1200);   // an instance not seen yet: nothing
    book.Seen(seen, 1500);           // startedAt kept
    book.Close(1700, 0);
    assert(book.Running() == 1);     // gone less than 5 min
    book.Kill(7, 645, true, 1750);   // the last boss: cleared
    book.Close(1500 + RUN_GONE_S, 0);
    assert(book.Running() == 0);
    j = book.Json();
    assert(j.find("\"today\":{\"since\":0,\"runs\":1,\"cleared\":1,\"wipes\":1,\"bossKills\":2}") != std::string::npos);
    assert(j.find("\"last\":[{\"instance\":7,") != std::string::npos);
    assert(j.find("\"startedAt\":1000,\"endedAt\":1500,\"result\":\"clear\",\"bossesKilled\":2,\"bossesTotal\":6,"
                  "\"wipes\":1,") != std::string::npos);
    assert(j.find("\"byDungeon\":[{\"dungeon\":null,\"map\":36,\"dungeonName\":\"The Deadmines\",\"runs\":1,"
                  "\"cleared\":1,\"wipes\":1,\"medianS\":500,\"bestS\":500}]") != std::string::npos);

    // A wipe and then the group gone: "wipe"; nothing: "left".
    Run other = seen;
    other.instance = 8;
    book.Seen(other, 5000);
    book.Wipe(8);
    other.instance = 9;
    book.Seen(other, 5000);
    book.Close(5000 + RUN_GONE_S, 0);
    j = book.Json();
    assert(j.find("\"instance\":8,") != std::string::npos && j.find("\"result\":\"wipe\"") != std::string::npos &&
           j.find("\"result\":\"left\"") != std::string::npos);

    // A new day: "today" starts again.
    book.Close(90000, 86400);
    assert(book.Json().find("\"today\":{\"since\":86400,\"runs\":0,") != std::string::npos);
    assert(TodaySince(86400, 90000) == 90000 && TodaySince(86400, 100) == 86400);

    // The last 8 runs only.
    for (uint32_t i = 100; i < 120; ++i)
    {
        Run r = seen;
        r.instance = i;
        book.Seen(r, 200000);
    }
    book.Close(200000 + RUN_GONE_S, 86400);
    std::string const k = book.Json();
    size_t const lastAt = k.find("\"last\":[");
    size_t count = 0;
    for (size_t at = k.find("\"result\"", lastAt); at != std::string::npos; at = k.find("\"result\"", at + 1))
        ++count;
    assert(count == RUNS_LAST);
    return 0;
}
