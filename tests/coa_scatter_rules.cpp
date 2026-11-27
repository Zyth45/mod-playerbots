#include "CoaScatterRules.h"

#include <cassert>
#include <cmath>

using namespace CoaScatterRules;

int main()
{
    Point const first = Destination(0.0f, 0.0f, 0.0f, 8.0f, 0, 4, 0);
    Point const second = Destination(0.0f, 0.0f, 0.0f, 8.0f, 1, 4, 0);
    Point const farther = Destination(0.0f, 0.0f, 0.0f, 8.0f, 0, 4, 1);

    assert(std::fabs(std::hypot(first.x, first.y) - 8.0f) < 0.001f);
    assert(std::hypot(first.x - second.x, first.y - second.y) > 8.0f);
    assert(std::fabs(std::hypot(farther.x, farther.y) - 17.0f) < 0.001f);
    return 0;
}
