// CoA 3.3.1: the copy of an item that scored as an upgrade is the one put on (CoaEquipPickRules.h).
#include "CoaEquipPickRules.h"

#include <cassert>

using CoaEquipPickRules::Chosen;

int main()
{
    // Farza (bench C2): two Lesser Magic Wands in the bags, only the second (13037) beats the worn Greater one.
    auto c = Chosen({{11287, false}, {2559, false}, {11287, true}});
    assert(c.size() == 1 && c.at(11287) == 2);  // the copy that scored, not the first one of that id
    c = Chosen({{11287, true}, {11287, true}});
    assert(c.at(11287) == 0);
    c = Chosen({{11287, false}, {11288, false}});
    assert(c.empty());  // nothing better: nothing put on
    c = Chosen({{11287, true}, {11288, true}});
    assert(c.size() == 2 && c.at(11288) == 1);
    return 0;
}
