#include "CoaChanAskRules.h"

#include <cassert>

int main()
{
    using namespace CoaChanAsk;

    // The class question, in English and French.
    assert(Classify("what class is the best?") == Topic::Class);
    assert(Classify("Which class should I play") == Topic::Class);
    assert(Classify("quelle est la meilleure classe ?") == Topic::Class);
    assert(Classify("je debute, quelle classe a jouer ?") == Topic::Class);

    // Other open questions.
    assert(Classify("anyone know where the flight master is?") == Topic::Question);
    assert(Classify("how do you get to Ironforge") == Topic::Question);
    assert(Classify("quelqu'un sait ou est le maitre de vol ?") == Topic::Question);

    // Left to CoA World: every guild word.
    assert(Classify("is there a guild i can join?") == Topic::None);
    assert(Classify("any guilds recruiting?") == Topic::None);
    assert(Classify("une guilde a rejoindre ?") == Topic::None);

    // Groups, trade, bots, advances, commands, links: no answer.
    assert(Classify("LFM deadmines need tank?") == Topic::None);
    assert(Classify("how much for linen cloth?") == Topic::None);
    assert(Classify("are you all bots?") == Topic::None);
    assert(Classify("any cute girl wants to be my gf?") == Topic::None);
    assert(Classify(".playerbots coa tank?") == Topic::None);
    assert(Classify("anyone need |Hitem:2589:0|h[Linen Cloth]|h?") == Topic::None);

    // Not a question, too short, too long.
    assert(Classify("going to deadmines now") == Topic::None);
    assert(Classify("?") == Topic::None);
    assert(Classify("lol") == Topic::None);
    assert(Classify("") == Topic::None);
    assert(Classify("what a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a?") == Topic::None);

    // A word that only contains a blocked one is fine.
    assert(Classify("which bottle heals the most?") == Topic::Question);

    assert(PlayerMayAsk(0, 100));
    assert(!PlayerMayAsk(100, 100 + PLAYER_CD_S - 1));
    assert(PlayerMayAsk(100, 100 + PLAYER_CD_S));
    assert(PLAYER_CD_S <= 60);
    assert(ChannelMayAnswer(0, 5));
    assert(!ChannelMayAnswer(100, 100 + CHANNEL_GAP_S - 1));
    assert(ChannelMayAnswer(100, 100 + CHANNEL_GAP_S));
    return 0;
}
