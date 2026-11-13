#include "CoaLootAskRules.h"

#include <cassert>

int main()
{
    using namespace CoaLootAsk;

    // Yes and no in both languages.
    for (char const* y : {"yes", "Yes!", "y", "sure", "ok go", "np", "oui", "ouais", "vas-y", "vas y", "yeah take it",
                          "OK"})
        assert(Classify(y) == Answer::Yes);
    for (char const* n : {"no", "No.", "nope", "nah", "non", "nan", "don't", "pas question", "non merci"})
        assert(Classify(n) == Answer::No);
    // Ambiguous, unrelated or too long: no answer.
    assert(Classify("yes no") == Answer::None);
    assert(Classify("oui non") == Answer::None);
    assert(Classify("lol") == Answer::None);
    assert(Classify("") == Answer::None);
    assert(Classify("ok so who pulls the next pack") == Answer::None);
    assert(Classify("what") == Answer::None);
    // The player's own claim is no yes.
    assert(Classify("i need it") == Answer::None);
    assert(Classify("je le prends") == Answer::None);
    assert(Classify("mine") == Answer::None);

    // One real player says yes: Need at once.
    Ask one;
    one.bot = 1;
    one.deadlineMs = 1000 + WINDOW_MS;
    one.players = {100};
    assert(Decide(one, 1000) == Outcome::Wait);
    Hear(one, 100, Classify("sure"), 2000);
    assert(Decide(one, 2000) == Outcome::Need);

    // A no: Greed. A no after a yes still wins while the vote waits.
    Ask refused = one;
    refused.yes.clear();
    Hear(refused, 100, Classify("non"), 2000);
    assert(Decide(refused, 2000) == Outcome::Greed);
    assert(std::string(AnswerName(refused)) == "no");
    assert(std::string(AnswerName(one)) == "yes");
    Ask silent;
    assert(std::string(AnswerName(silent)) == "none");

    // Several players: one no wins over a yes, whatever the order.
    Ask two;
    two.deadlineMs = 1000 + WINDOW_MS;
    two.players = {100, 200};
    Hear(two, 100, Answer::Yes, 2000);
    assert(Decide(two, 2000) == Outcome::Wait);  // waits for the other one
    Hear(two, 200, Answer::No, 3000);
    assert(Decide(two, 3000) == Outcome::Greed);
    Ask both = two;
    both.no = false;
    both.yes = {100, 200};
    assert(Decide(both, 3000) == Outcome::Need);
    // A yes and silence: Need at the end of the window only.
    Ask half = two;
    half.no = false;
    half.yes = {100};
    assert(Decide(half, 1000 + WINDOW_MS - 1) == Outcome::Wait);
    assert(Decide(half, 1000 + WINDOW_MS) == Outcome::Need);

    // Window over with no answer: Greed. An answer after the window or from outside the group does not count.
    Ask late = one;
    late.yes.clear();
    assert(Decide(late, 1000 + WINDOW_MS) == Outcome::Greed);
    Hear(late, 100, Answer::Yes, 1000 + WINDOW_MS);
    assert(late.yes.empty());
    Hear(late, 999, Answer::Yes, 2000);
    assert(late.yes.empty());

    // Two asks per instance for all the bots, one bot per roll.
    Book book;
    Ask a;
    a.deadlineMs = WINDOW_MS;
    assert(book.CanOpen(7, 1, 2));
    assert(book.Open(7, 1, a, 2));
    assert(!book.CanOpen(7, 1, 2));
    assert(!book.Open(7, 1, a, 2));  // same roll: another bot does not ask
    assert(book.Open(7, 2, a, 2));
    assert(!book.CanOpen(7, 3, 2));
    assert(!book.Open(7, 3, a, 2));  // third ask in the instance
    assert(book.Open(8, 3, a, 2));   // another instance has its own share
    book.Forget(WINDOW_MS * 4);
    assert(!book.Find(1));
    assert(!book.Open(7, 4, a, 2));  // the share of the instance stays after the rolls are forgotten
    return 0;
}
