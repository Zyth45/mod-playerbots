// Unit check for CoaClassNames.h (the class a name asks for, .playerbots addclass included).
#include "CoaClassNames.h"
#include <cassert>
#include <cstring>

int main()
{
    // The original classes, by their short names, as before.
    assert(AddclassClassFromName("warrior") == 1 && AddclassClassFromName("dk") == 6 && AddclassClassFromName("druid") == 11);
    assert(AddclassClassFromName("Mage") == 8);
    // 3.2.3: every CoA class, by its full name in any form, 12 to 32.
    for (std::uint8_t id = COA_FIRST_CLASS; id < COA_END_CLASS; ++id)
        assert(AddclassClassFromName(CoaClassName(id)) == id);
    assert(COA_END_CLASS == 33);
    assert(AddclassClassFromName("witchdoctor") == 13 && AddclassClassFromName("witch_doctor") == 13);
    assert(AddclassClassFromName("Knight-of-Xoroth") == 17);
    assert(AddclassClassFromName("sun") == 27);   // a beginning no other class shares
    assert(AddclassClassFromName("witch") == 0);  // Witch Doctor and Witch Hunter: say which
    assert(AddclassClassFromName("") == 0 && AddclassClassFromName("deathknight") == 0);
    assert(CoaClassName(11) == nullptr && CoaClassName(33) == nullptr);
    assert(std::strcmp(CoaClassName(32), "Runemaster") == 0);
    return 0;
}
