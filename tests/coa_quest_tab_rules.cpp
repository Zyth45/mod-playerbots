#include "CoaQuestTabRules.h"

#include <cassert>

int main()
{
    using namespace CoaQuestTab;

    assert(std::string(StateName(false, false)) == "inprog" && std::string(StateName(true, false)) == "done" &&
           std::string(StateName(false, true)) == "failed");
    assert(Objectives({{3, 10}, {12, 8}, {0, 0}, {0, 1}}) == "3/10;8/8;0/1");
    assert(Objectives({}).empty());

    assert(Line(783, "inprog", "2/10", "Elwynn Forest", "A Threat Within") ==
           "QL\tit\t783\tinprog\t2/10\tElwynn Forest\tA Threat Within");
    // Tabs never pass; the title is cut at 80 and the line stays under 250.
    assert(Line(1, "done", "", "a\tb", "x\ty") == "QL\tit\t1\tdone\t\ta b\tx y");
    std::string const longTitle(300, 'T');
    std::string const zone(100, 'Z');
    std::string const line = Line(99999, "inprog", std::string(200, '1'), zone, longTitle);
    assert(line.size() <= MAX_LINE);
    assert(Line(5, "inprog", "", "", longTitle).size() == std::string("QL\tit\t5\tinprog\t\t\t").size() + MAX_TITLE);
    // A UTF-8 character is never cut in two.
    assert(Clean("\xC3\xA9\xC3\xA9", 3) == "\xC3\xA9");

    uint32_t questId = 0, choice = 0;
    assert(ParseTurnIn("783", questId, choice) && questId == 783 && choice == 0);
    assert(ParseTurnIn("783 2", questId, choice) && questId == 783 && choice == 2);
    assert(!ParseTurnIn("", questId, choice) && !ParseTurnIn("abc", questId, choice) &&
           !ParseTurnIn("783 7", questId, choice) && !ParseTurnIn("0", questId, choice) &&
           !ParseTurnIn("783 x", questId, choice));

    assert(Decide(false, false, false, 0, 0, false) == TurnIn::NotInLog);
    assert(Decide(true, false, true, 0, 0, true) == TurnIn::NotComplete);
    assert(Decide(true, true, false, 0, 0, true) == TurnIn::NoEnder);
    assert(Decide(true, true, true, 3, 0, true) == TurnIn::ChooseReward);
    assert(Decide(true, true, true, 3, 4, true) == TurnIn::ChooseReward);
    assert(Decide(true, true, true, 3, 2, false) == TurnIn::BagsFull);
    assert(Decide(true, true, true, 3, 2, true) == TurnIn::TurnedIn);
    assert(Decide(true, true, true, 1, 0, true) == TurnIn::TurnedIn);
    assert(std::string(TurnInName(TurnIn::NoEnder)) == "no_ender");

    bool on = false;
    assert(ParseWatch("watch on", on) && on && ParseWatch("watch off", on) && !on && !ParseWatch("watch", on));
    assert(Watching(100, 99) && !Watching(100, 100) && !Watching(0, 5));
    return 0;
}
