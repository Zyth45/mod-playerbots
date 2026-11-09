#include "CoaLiveRules.h"

#include <cassert>
#include <random>
#include <set>

using namespace CoaLive;

namespace
{
std::mt19937 gen(42);
Rng const rng{[](uint32_t lo, uint32_t hi) { return std::uniform_int_distribution<uint32_t>(lo, hi)(gen); }};
Rng const always{[](uint32_t lo, uint32_t) { return lo; }};  // every roll succeeds, the first choice

Line L(uint32_t id, std::string situation, uint8_t locale, std::string text, uint8_t band = 0, bool rare = false)
{
    Line l;
    l.id = id;
    l.situation = std::move(situation);
    l.locale = locale;
    l.text = std::move(text);
    l.levelBand = band;
    l.rare = rare;
    return l;
}
}  // namespace

int main()
{
    // Placeholders and filling: a line with an empty placeholder is not said.
    assert((Placeholders("hey %player, %subzone is nice 100%") == std::vector<std::string>{"player", "subzone"}));
    std::string out;
    assert(Fill("hey %player in %subzone", {{"player", "Arthas"}, {"subzone", "Goldshire"}}, out) &&
           out == "hey Arthas in Goldshire");
    assert(!Fill("wts %item", {}, out));
    assert(!Fill("wts %item", {{"item", ""}}, out));
    assert(Fill("100% sure", {}, out) && out == "100% sure");

    // The bank: French first, English when the situation has no French line.
    Bank bank;
    bank.rules["greet_pass"] = {25, 600, 2500, 600, 20, 6, true};
    bank.Add(L(1, "greet_pass", 0, "hey"));
    bank.Add(L(2, "greet_pass", 0, "hi %player"));
    bank.Add(L(3, "greet_pass", 2, "salut"));
    bank.Add(L(4, "duel_won", 0, "gg"));
    bank.Add(L(5, "trade_wts", 0, "wts %item %price"));
    bank.Add(L(6, "greet_pass", 0, "lol", 0, true));
    bank.Add(L(7, "greet_pass", 0, "grats on 60", 4));
    assert(bank.LinesOf("greet_pass", 2)->size() == 1);
    assert(bank.LinesOf("duel_won", 2)->front().id == 4);
    assert(bank.LinesOf("nothing", 0) == nullptr);
    assert(bank.LineCount() == 7);

    // The draw skips a line whose placeholder has no value, rare lines without the rare roll, other level bands,
    // and the lines of the rings.
    for (int i = 0; i < 200; ++i)
    {
        Pick p = Draw(bank, "greet_pass", 0, 2, ARCH_NONE, false, nullptr, nullptr, {}, rng);
        assert(p.line && p.line->id == 1);
    }
    assert(!Draw(bank, "trade_wts", 0, 4, ARCH_NONE, false, nullptr, nullptr, {{"item", "[Linen]"}}, rng).line);
    std::set<uint32_t> seen;
    for (int i = 0; i < 400; ++i)
        seen.insert(Draw(bank, "greet_pass", 0, 4, ARCH_NONE, true, nullptr, nullptr, {{"player", "Jaina"}}, rng).line->id);
    assert((seen == std::set<uint32_t>{1, 2, 6, 7}));
    Ring botRing(2), channelRing(200);
    botRing.Push(1);
    channelRing.Push(2);
    assert(!Draw(bank, "greet_pass", 0, 2, ARCH_NONE, false, &botRing, &channelRing, {{"player", "Jaina"}}, rng).line);
    botRing.Push(9);
    botRing.Push(10);  // the ring forgets the oldest id
    assert(!botRing.Has(1) && botRing.Size() == 2);

    // Soul weights: the trader says its typical line far more often.
    Bank souls;
    Line typical = L(1, "s", 0, "a");
    typical.weight[ARCH_TRADER] = 18;
    typical.weight[ARCH_QUESTER] = 3;
    souls.Add(typical);
    souls.Add(L(2, "s", 0, "b"));
    int trader = 0, quester = 0;
    for (int i = 0; i < 2000; ++i)
    {
        trader += Draw(souls, "s", 0, 1, ARCH_TRADER, false, nullptr, nullptr, {}, rng).line->id == 1;
        quester += Draw(souls, "s", 0, 1, ARCH_QUESTER, false, nullptr, nullptr, {}, rng).line->id == 1;
    }
    assert(trader > 1150 && quester < 500);

    // Slang: whole words and phrases only, never inside a placeholder.
    std::vector<std::pair<std::string, std::string>> slang;
    Bank sb;
    sb.AddSlang(0, "thanks", "thx");
    sb.AddSlang(0, "no problem", "np");
    sb.AddSlang(0, "congratulations", "gz");
    assert(sb.SlangOf(0).front().first == "congratulations");
    assert(Humanize("Thanks %player, no problem", {{"player", "Thanksgiver"}}, sb.SlangOf(0), 100, 0, false, always, out) &&
           out == "thx Thanksgiver, np");
    assert(Humanize("thanksgiving", {}, sb.SlangOf(0), 100, 0, false, always, out) && out == "thanksgiving");
    assert(Humanize("no problemo", {}, sb.SlangOf(0), 100, 0, false, always, out) && out == "no problemo");

    // Typos: at most one change, never in a filled placeholder, never three letters in a row.
    for (int i = 0; i < 2000; ++i)
    {
        assert(Humanize("Good luck with %item out there.", {{"item", "[Thunderfury]"}}, {}, 0, 100, false, rng, out));
        assert(out.find("[Thunderfury]") != std::string::npos);
        int diff = static_cast<int>(out.size()) - static_cast<int>(std::string("Good luck with [Thunderfury] out there.").size());
        assert(diff >= -1 && diff <= 1);
        for (size_t k = 2; k < out.size(); ++k)
            assert(!(out[k] == out[k - 1] && out[k] == out[k - 2] && AsciiLetter(out[k])));
    }
    // French: an accent may be forgotten.
    bool unaccented = false;
    for (int i = 0; i < 2000 && !unaccented; ++i)
    {
        assert(Humanize("bien jou\xC3\xA9 %player", {{"player", "\xC3\x89lodie"}}, {}, 0, 100, true, rng, out));
        assert(out.find("\xC3\x89lodie") != std::string::npos);
        unaccented = out == "bien joue \xC3\x89lodie";
    }
    assert(unaccented);
    assert(Humanize("hey", {}, {}, 0, 0, false, rng, out) && out == "hey");

    // Typing time: the rule's thinking time plus the length.
    Rule r{100, 1000, 2000, 0, 0, 0, true};
    for (int i = 0; i < 100; ++i)
    {
        uint32_t t = TypingMs(r, 10, 60, rng);
        assert(t >= 1600 && t <= 2600);
    }

    // Whispers: lower case already, punctuation and apostrophes become spaces, whole words only, by priority.
    Bank ib;
    ib.AddIntent(0, 70, "hello", "hi,hey,hello");
    ib.AddIntent(0, 10, "bot", "are you a bot,bot,npc");
    ib.AddIntent(0, 20, "group", "inv,group");
    ib.AddIntent(2, 10, "bot", "t es un bot,bot");
    assert(NormalizeWhisper("hey, are you a bot?!") == " hey are you a bot ");
    assert(Classify(NormalizeWhisper("hey, are you a bot?!"), ib.IntentsOf(0)) == "bot");
    assert(Classify(NormalizeWhisper("hi can u inv"), ib.IntentsOf(0)) == "group");
    assert(Classify(NormalizeWhisper("this is nice"), ib.IntentsOf(0)) == "fallback");  // "this" is not "hi"
    assert(Classify(NormalizeWhisper("botany"), ib.IntentsOf(0)) == "fallback");
    assert(Classify(NormalizeWhisper("t'es un bot ?"), ib.IntentsOf(2)) == "bot");
    assert(Classify(NormalizeWhisper("hello"), ib.IntentsOf(3)) == "hello");  // no locale 3: English

    // "where": the subject, longest first, without the little words.
    std::vector<std::string> const where{" where ", " how do i get "};
    std::vector<std::string> subj = WhereSubjects(NormalizeWhisper("hey where is the goldshire inn?"), where);
    assert((subj == std::vector<std::string>{"goldshire inn", "goldshire"}));
    subj = WhereSubjects(NormalizeWhisper("how do i get to stormwind"), where);
    assert((subj == std::vector<std::string>{"stormwind"}));
    subj = WhereSubjects(NormalizeWhisper("where is hogger the big bad gnoll"), where);
    assert(subj.size() == 4 && subj.front() == "hogger the big bad" && subj.back() == "hogger");
    assert(WhereSubjects(NormalizeWhisper("where?"), where).empty());
    assert(WhereSubjects(NormalizeWhisper("hello"), where).empty());
    std::vector<std::string> const ou{" ou est ", " où "};
    subj = WhereSubjects(NormalizeWhisper("ou est la forge"), ou);
    assert((subj == std::vector<std::string>{"forge"}));

    // Off, no whisper is ever taken: the 2.9.5 path (commands, free chat replies) stays exactly as it was.
    for (int mask = 0; mask < 8; ++mask)
        assert(!TakesWhisper(false, mask & 1, mask & 2, mask & 4));
    // On, only a real player's whisper to a random bot, never its master's, never a bot's.
    assert(TakesWhisper(true, true, true, false));
    assert(!TakesWhisper(true, true, true, true));
    assert(!TakesWhisper(true, false, true, false));
    assert(!TakesWhisper(true, true, false, false));

    // Pace: the bot's cooldown, the scope's cooldown and the cap per 10 minutes of a channel.
    Pacer pacer;
    Rule g{100, 0, 0, 600, 20, 3, true};
    assert(pacer.Allowed(g, 1, "greet_pass", "zone1", 0, 1000));
    pacer.Said(1, "greet_pass", "zone1", 0, 1000);
    assert(!pacer.Allowed(g, 1, "greet_pass", "zone1", 0, 1100));   // bot cooldown
    assert(!pacer.Allowed(g, 2, "greet_pass", "zone1", 0, 1010));   // scope cooldown
    assert(pacer.Allowed(g, 2, "greet_pass", "zone1", 0, 1020));
    assert(pacer.Allowed(g, 2, "greet_pass", "zone2", 0, 1010));    // another place
    pacer.Said(2, "greet_pass", "zone1", 0, 1020);
    pacer.Said(3, "greet_pass", "zone1", 0, 1040);
    assert(!pacer.Allowed(g, 4, "greet_pass", "zone1", 0, 1060));   // 3 in 10 minutes
    assert(pacer.Allowed(g, 4, "greet_pass", "zone1", 0, 1601));    // the first one is 10 minutes old
    assert(pacer.Allowed(g, 1, "greet_pass", "zone1", 0, 1601));
    pacer.Prune(1000 + 86400 + 100);
    assert(pacer.Size() == 0);

    // Chance to react.
    assert(ReactPct(r, -1, 1.0f) == 100);
    Rule c{40, 0, 0, 0, 0, 0, true};
    assert(ReactPct(c, -1, 1.0f) == 40);
    assert(ReactPct(c, 50, 1.0f) == 40);
    assert(ReactPct(c, 100, 1.0f) == 60);
    assert(ReactPct(c, -1, PopulationFactor(0)) == 8);
    assert(PopulationFactor(3) == 1.0f && PopulationFactor(30) == 1.0f);
    assert(FormatMoney(125000) == "12g 50s" && FormatMoney(120000) == "12g" && FormatMoney(7500) == "75s" &&
           FormatMoney(8) == "8c" && FormatMoney(0) == "0c" && FormatMoney(10099) == "1g");
    assert(LevelBand(1) == 1 && LevelBand(19) == 1 && LevelBand(20) == 2 && LevelBand(59) == 3 && LevelBand(60) == 4);
    // Night bank: situations, filters, rules, conversations.
    {
        assert(NightSituation(CH_GENERAL, "idle_class") == "night_zone");
        assert(NightSituation(CH_WORLD, "idle_lfg") == "night_world");
        assert(NightSituation(CH_PARTY, "dgn_boss_kill") == "night_dgn_boss_kill");
        assert(NightSituation(CH_GENERAL, "thread").empty() && NightSituation(7, "idle_general").empty());

        Bank night;
        Line mage = L(900001, "night_zone", 2, "Les mages ont froid");
        mage.channel = CH_GENERAL;
        mage.cls = 8;
        night.Add(mage);
        Line elwynn = L(900002, "night_zone", 2, "Encore les kobolds de %zone");
        elwynn.channel = CH_GENERAL;
        elwynn.zone = 12;
        night.Add(elwynn);
        Who const warriorInDurotar{2, 1, 14};
        assert(!Draw(night, "night_zone", 2, 1, ARCH_NONE, false, nullptr, nullptr, {{"zone", "Durotar"}}, rng,
                     warriorInDurotar).line);
        Who const mageInElwynn{1, 8, 12};
        std::set<uint32_t> got;
        for (int i = 0; i < 50; ++i)
            got.insert(Draw(night, "night_zone", 2, 1, ARCH_NONE, false, nullptr, nullptr, {{"zone", "Elwynn"}}, rng,
                            mageInElwynn).line->id);
        assert(got.size() == 2);
        // Without a Who (the old callers), the filtered lines stay out.
        assert(!Draw(night, "night_zone", 2, 1, ARCH_NONE, false, nullptr, nullptr, {{"zone", "Elwynn"}}, rng).line);

        Line pull = L(900003, "night_dgn_pull_big", 0, "big pull");
        pull.channel = CH_PARTY;
        night.Add(pull);
        night.rules["general_chat"] = Rule{40, 1, 2, 3, 4, 0, true};
        night.rules["night_world"] = Rule{5, 1, 2, 3, 4, 0, true};  // the bank's own rule wins
        AddNightRules(night);
        assert(night.RuleOf("night_zone")->chancePct == 40 && night.RuleOf("night_thread")->chancePct == 40);
        assert(night.RuleOf("night_world")->chancePct == 5 && night.RuleOf("night_dgn_pull_big"));

        Thread tank;
        tank.levelBand = 1;
        tank.steps = {{1, "Je cherche un tank pour Deadmines"}, {2, "je peux tanker"}};
        night.threads[CH_GENERAL][2].push_back(tank);
        Thread trio;
        trio.steps = {{1, "a"}, {2, "b"}, {3, "c"}};
        night.threads[CH_GENERAL][0].push_back(trio);
        assert(DrawThread(night, CH_GENERAL, 2, 1, mageInElwynn, 2, rng)->Speakers() == 2);
        assert(!DrawThread(night, CH_GENERAL, 2, 3, mageInElwynn, 5, rng));  // the French one is of band 1
        assert(DrawThread(night, CH_GENERAL, 0, 3, mageInElwynn, 3, rng) == &night.threads[CH_GENERAL][0][0]);
        assert(!DrawThread(night, CH_GENERAL, 0, 3, mageInElwynn, 2, rng));  // three speakers, two bots
        assert(!DrawThread(night, CH_WORLD, 0, 3, mageInElwynn, 3, rng));
    }

    // Trade pace: 30 to 90 s for one a minute, never under 5 s; a speaker not heard lately.
    {
        assert(TradeGapMs(0, rng) == 0);
        for (int k = 0; k < 200; ++k)
        {
            uint32_t const gap = TradeGapMs(1, rng);
            assert(gap >= 30000 && gap <= 90000);
            uint32_t const g3 = TradeGapMs(3, rng);
            assert(g3 >= 10000 && g3 <= 30000);
        }
        assert(TradeGapMs(100, rng) >= 5000);
        std::vector<uint64_t> const ids{11, 22, 33};
        for (int k = 0; k < 50; ++k)
            assert(PickSpeaker(3, ids, {11, 33}, rng) == 1);
        assert(PickSpeaker(3, ids, {11, 22, 33}, rng) < 3);  // everyone heard lately: anyone
        assert(PickSpeaker(0, {}, {}, rng) == SIZE_MAX);
    }

    return 0;
}
