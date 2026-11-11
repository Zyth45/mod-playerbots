#include "CoaChatRules.h"

#include <cassert>

int main()
{
    CoaChat::Parts on = CoaChat::PartsOf(true);
    assert(on.text && on.emote && on.invite && on.cod);
    CoaChat::Parts off = CoaChat::PartsOf(false);
    assert(!off.text && off.emote && off.invite && off.cod);
    assert(CoaChat::TradePaceRuns(true, 2) && !CoaChat::TradePaceRuns(false, 2) && !CoaChat::TradePaceRuns(true, 0));
    assert(CoaChat::ChannelLineAllowed(true) && !CoaChat::ChannelLineAllowed(false));
    assert(CoaChat::ChatReplyAllowed(true, false) && CoaChat::ChatReplyAllowed(false, true) &&
           !CoaChat::ChatReplyAllowed(false, false));
    // Older lines: silent while the living chat speaks, unless LegacyChat; as before with CoA Live off.
    assert(!CoaChat::LegacyLinesAllowed(true, true, false));
    assert(CoaChat::LegacyLinesAllowed(true, true, true));
    assert(CoaChat::LegacyLinesAllowed(false, true, false));
    assert(!CoaChat::LegacyLinesAllowed(false, false, true) && !CoaChat::LegacyLinesAllowed(true, false, true));
    return 0;
}
