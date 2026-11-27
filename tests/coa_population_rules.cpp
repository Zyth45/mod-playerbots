// Unit check for CoaPopulationRules.h (the periodic online-offline's bot total and accounts in use).
#include "CoaPopulationRules.h"
#include <cassert>

int main()
{
    // 3.2.3: the default ratio is 1.5, 1,000 online for 1,500 kept.
    assert(COA_PERIODIC_RATIO_DEFAULT == 1.5f);
    assert(CoaPeriodicBotTotal(1000, true, COA_PERIODIC_RATIO_DEFAULT) == 1500);
    assert(CoaPeriodicBotTotal(500, true, COA_PERIODIC_RATIO_DEFAULT) == 750);
    assert(CoaPeriodicBotTotal(1001, true, 1.5f) == 1502);  // 1501.5 rounded up
    assert(CoaPeriodicBotTotal(1000, true, 2.0f) == 2000);  // an edited 2.0 kept as before
    assert(CoaPeriodicBotTotal(1000, false, 1.5f) == 1000); // periodic off: the ratio plays no part
    assert(CoaPeriodicBotTotal(1000, true, 0.5f) == 1000);
    // Accounts in use: rounded up.
    assert(CoaPeriodicAccountsToUse(5, 1.5f) == 4);
    assert(CoaPeriodicAccountsToUse(3, 1.5f) == 2);
    assert(CoaPeriodicAccountsToUse(100, 2.0f) == 50);
    assert(CoaPeriodicAccountsToUse(101, 2.0f) == 51);
    assert(CoaPeriodicAccountsToUse(7, 1.0f) == 7);
    return 0;
}
