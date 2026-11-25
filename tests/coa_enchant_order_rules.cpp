// CoA 3.3.1: the enchantment orders' pure rules (CoaEnchantOrderRules.h).
#include "CoaEnchantOrderRules.h"

#include <cassert>
#include <cstdio>

using namespace CoaEnchantOrderRules;

int main()
{
    // Who orders.
    assert(Orders(20, 20, false));
    assert(!Orders(19, 20, false));
    assert(!Orders(60, 20, true));  // an enchanter enchants for itself

    // A level 60 bot orders twice as often.
    assert(IntervalSeconds(59, 8, 60) == 8 * 3600);
    assert(IntervalSeconds(60, 8, 60) == 4 * 3600);
    assert(IntervalSeconds(30, 0, 60) == 3600);  // never 0

    // Pieces: epic before rare before green; greens only close to the level and below 60.
    uint32_t const epic = PieceRank(QualityEpic, 66, 60, 60);
    uint32_t const rare = PieceRank(QualityRare, 70, 60, 60);
    assert(epic > rare && rare > 0);
    assert(PieceRank(QualityUncommon, 60, 60, 60) == 0);   // a green at 60 is replaced soon
    assert(PieceRank(QualityUncommon, 28, 30, 60) > 0);    // close to its level
    assert(PieceRank(QualityUncommon, 20, 30, 60) == 0);   // far below it
    assert(PieceRank(1, 60, 60, 60) == 0);                  // white: never
    assert(PieceRank(QualityEpic, 70, 60, 60) > PieceRank(QualityEpic, 66, 60, 60));

    // Fee: a share of the reagents, at least a floor per level.
    assert(Fee(100000, 20, 60, 50) == 20000);
    assert(Fee(1000, 20, 60, 50) == 3000);

    // Cap: never more than the share of its gold, nor its level's allowance.
    assert(SpendCap(1000000, 25, 60, 10000) == 250000);
    assert(SpendCap(10000000, 25, 60, 10000) == 600000);
    assert(SpendCap(0, 25, 60, 10000) == 0);
    assert(SpendCap(1000, 250, 60, 10000) == 1000);  // a share over 100 % is 100 %

    // The best enchantment it can pay for.
    assert(Better(10.0f, 500, 1000, 5.0f));
    assert(!Better(10.0f, 1500, 1000, 5.0f));  // too dear: a cheaper one may still win
    assert(!Better(4.0f, 500, 1000, 5.0f));
    assert(!Better(0.0f, 0, 1000, -1.0f));     // no gain

    std::puts("coa_enchant_order_rules: OK");
    return 0;
}
