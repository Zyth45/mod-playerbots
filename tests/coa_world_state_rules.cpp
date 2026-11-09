#include "CoaWorldStateRules.h"

#include <cassert>

using namespace CoaWorldStateRules;

int main()
{
    assert(Quote("a\"b\\c") == "\"a\\\"b\\\\c\"");
    assert(Quote("x\ny") == "\"x\\ny\"");
    assert(Quote(std::string("\x01")) == "\"\\u0001\"");
    assert(DateOrNull(0) == "null" && DateOrNull(12) == "12");

    // The topic must be asked, and the request fresh.
    std::string const watch = "{\"at\": 1000, \"topics\": [\"world\", \"monde\"]}";
    assert(Watched(watch, "monde", 0, 1050));
    assert(!Watched(watch, "monde", 0, 1090));
    assert(!Watched(watch, "economy", 0, 1050));
    assert(!Watched("{\"topics\":[\"mondes\"]}", "monde", 1000, 1010));
    assert(Watched("{\"topics\":[\"monde\"]}", "monde", 1000, 1010));  // the file's date without "at"

    std::deque<int64_t> dates{ 10, 50000, 90000 };
    assert(LastDay(dates, 90000) == 2);
    assert(LastDay(dates, 90000 + 86400) == 0);
    return 0;
}
