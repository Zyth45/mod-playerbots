#include "CoaChatRules.h"

#include <cassert>

int main()
{
    CoaChat::Parts on = CoaChat::PartsOf(true);
    assert(on.text && on.emote && on.invite && on.cod);
    CoaChat::Parts off = CoaChat::PartsOf(false);
    assert(!off.text && off.emote && off.invite && off.cod);
    assert(CoaChat::TradePaceRuns(true, 2) && !CoaChat::TradePaceRuns(false, 2) && !CoaChat::TradePaceRuns(true, 0));
    return 0;
}
