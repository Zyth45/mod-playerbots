#include "CoaQuickAskRules.h"

#include <cassert>

int main()
{
    using namespace CoaQuickAsk;

    // The asks, the word alone for heal and taunt only.
    for (char const* ask : {"heal please", "heal me", "HEAL PLZ!", "heal", "need heal", "soigne-moi", "heal me now"})
        assert(ParseLine(ask) == Kind::Heal);
    for (char const* ask : {"cc please", "cc pls", "CC plz", "cc this", "cc target", "cc this please", "cc it stp"})
        assert(ParseLine(ask) == Kind::Cc);
    for (char const* ask : {"taunt please", "taunt", "Taunt!", "taunt it", "taunt this pls", "taunt now"})
        assert(ParseLine(ask) == Kind::Taunt);
    // "cc" alone is "coucou" (15/11); a sentence holding the word is talk.
    for (char const* talk : {"cc", "cc !", "cc tout le monde", "cc broke", "nice taunt", "taunt the boss please",
                             "who has a cc", "nice heal", "heal it", "the tank should taunt", "", "cc cc"})
        assert(ParseLine(talk) == Kind::None);

    // The addon's ask.
    Kind kind = Kind::None;
    uint64_t guid = 0;
    assert(ParseAddon("heal", kind, guid) && kind == Kind::Heal && guid == 0);
    assert(ParseAddon("cc F130000123000ABC", kind, guid) && kind == Kind::Cc && guid == 0xF130000123000ABCull);
    assert(ParseAddon("taunt 0x1A", kind, guid) && kind == Kind::Taunt && guid == 0x1A);
    assert(!ParseAddon("bloodlust", kind, guid) && !ParseAddon("cc zz", kind, guid) &&
           !ParseAddon("cc 11112222333344445", kind, guid));

    // The lines of the contract with the addon.
    assert(DoneLine(Kind::Cc, "Bob", 118, 0) == "QA\tdone\tcc\tBob\t118\t0");
    assert(NoneLine(Kind::Taunt, Reason::NoBot) == "QA\tnone\ttaunt\tnotank");
    assert(NoneLine(Kind::Heal, Reason::NoBot) == "QA\tnone\theal\tnoheal");
    assert(NoneLine(Kind::Cc, Reason::NoBot) == "QA\tnone\tcc\tnocc");
    assert(NoneLine(Kind::Cc, Reason::Immune) == "QA\tnone\tcc\timmune");
    assert(NoneLine(Kind::Taunt, Reason::Held) == "QA\tnone\ttaunt\theld");
    assert(NoneLine(Kind::Heal, Reason::Full) == "QA\tnone\theal\tfull");
    assert(StateLine(Kind::Taunt, 1, 0) == "QA\tst\ttaunt\t1\t0");

    // A bot that could have done it says more than nobody has one.
    assert(Stronger(Reason::NoBot, Reason::Cd) == Reason::Cd && Stronger(Reason::Range, Reason::Cd) == Reason::Range);
    assert(Stronger(Reason::Busy, Reason::NoBot) == Reason::Busy);

    // Crowd controls: a single enemy, no area, lasting 4 s or more (or with no end).
    assert(IsCrowdControl(false, false, true, true, 50000) && IsCrowdControl(false, false, true, true, -1));
    assert(!IsCrowdControl(false, false, true, true, 3000) && !IsCrowdControl(false, true, true, true, 10000) &&
           !IsCrowdControl(false, false, false, true, 10000) && !IsCrowdControl(true, false, true, true, 10000) &&
           !IsCrowdControl(false, false, true, false, 10000));
    // The longest first, then the shortest cooldown, then the lowest id.
    assert(BetterCc({118, 50000, 0}, {853, 6000, 60000}));
    assert(BetterCc({2, 10000, 0}, {1, 10000, 30000}) && BetterCc({1, 10000, 0}, {2, 10000, 0}));
    assert(BetterCc({5, -1, 0}, {6, 60000, 0}));

    // The categories of the short bar.
    assert(std::string(Category(true, false, 600000, false)) == "burst");
    assert(std::string(Category(false, true, 120000, false)) == "def");
    assert(std::string(Category(false, true, 30000, false)) == "-");
    assert(std::string(Category(false, false, 0, true)) == "cc");
    assert(std::string(Category(false, false, 300000, false)) == "-");

    // Windows: a cc or taunt answered in 1.5 s, a heal open 10 s, other roles help after 1 s with no cast.
    assert(Open(Kind::Cc, 1000, 2499) && !Open(Kind::Cc, 1000, 2500));
    assert(Open(Kind::Heal, 1000, 10999) && !Open(Kind::Heal, 1000, 11000));
    assert(!HealSilent(1000, 2499) && HealSilent(1000, 2500));
    assert(MayHeal(true, false, 1000, 1000) && !MayHeal(false, false, 1000, 1999) && MayHeal(false, false, 1000, 2000));
    assert(!MayHeal(false, true, 1000, 5000));
    assert(MayAsk(0, 5) && !MayAsk(1000, 2999) && MayAsk(1000, 3000));
    return 0;
}
