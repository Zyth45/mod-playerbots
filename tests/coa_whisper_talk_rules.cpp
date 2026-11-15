#include "CoaWhisperTalkRules.h"

#include <cassert>
#include <string>

int main()
{
    using namespace CoaWhisperTalk;

    // Five answers, then the excuse, then silence while the player insists; the silence does not grow.
    Pair pair;
    uint64_t now = 1000;
    for (int i = 0; i < 5; ++i, now += 30000)
        assert(Next(pair, now, 5, 1200000) == Step::Answer);
    assert(Next(pair, now, 5, 1200000) == Step::Escape);
    uint64_t const silentUntil = pair.silentUntilMs;
    assert(silentUntil == now + 1200000);
    for (int i = 0; i < 20; ++i)
    {
        now += 20000;
        assert(Next(pair, now, 5, 2400000) == Step::Silent);
    }
    assert(pair.silentUntilMs == silentUntil);
    // After the silence: a new talk, with the new limit.
    now = silentUntil;
    assert(Next(pair, now, 7, 1200000) == Step::Answer && pair.limit == 7 && pair.answers == 1);

    // Ten minutes without a whisper: the count starts again.
    Pair calm;
    now = 5000;
    for (int i = 0; i < 4; ++i, now += 1000)
        assert(Next(calm, now, 5, 1200000) == Step::Answer);
    now += RESET_MS;
    for (int i = 0; i < 5; ++i, now += 1000)
        assert(Next(calm, now, 5, 1200000) == Step::Answer);
    assert(Next(calm, now, 5, 1200000) == Step::Escape);

    // Stale: talk and silence both over.
    assert(!Stale(calm, now + 1000));
    assert(Stale(calm, calm.silentUntilMs + RESET_MS));
    assert(Stale(Pair(), 0));

    // The excuse fits the bot.
    assert(std::string(EscapeSituation(60, true, false, false, true)) == "wh_escape_busy");
    assert(std::string(EscapeSituation(30, false, true, false, true)) == "wh_escape_busy");
    assert(std::string(EscapeSituation(30, false, false, true, false)) == "wh_escape_busy");
    assert(std::string(EscapeSituation(8, false, false, false, true)) == "wh_escape_afk");
    assert(std::string(EscapeSituation(12, false, false, false, true)) == "wh_escape_bg");
    assert(std::string(EscapeSituation(40, false, false, false, true)) == "wh_escape_dungeon");
    assert(std::string(EscapeSituation(40, false, false, false, false)) == "wh_escape_bg");

    // "ça va" -> "ça va" -> "fine": never a loop.
    Pair talk;
    assert(Answered(talk, "wh_howru", 1000) == "wh_howru");
    assert(Answered(talk, "wh_howru", 61000) == "wh_fine");
    assert(Answered(talk, "wh_howru", 62000) == "wh_howru");
    assert(Answered(talk, "wh_howru", 62000 + HOWRU_AGAIN_MS) == "wh_howru");
    assert(Answered(talk, "wh_hello", 70000 + HOWRU_AGAIN_MS) == "wh_hello" && talk.howruMs == 0);
    assert(Answered(talk, "wh_howru", 80000 + HOWRU_AGAIN_MS) == "wh_howru");
    assert(!Stale(talk, 80000 + HOWRU_AGAIN_MS + 1000));
    return 0;
}
