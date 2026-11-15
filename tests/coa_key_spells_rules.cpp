#include "CoaKeySpellsRules.h"

#include <cassert>

int main()
{
    using namespace CoaKeySpells;

    // A long cooldown, active, not a profession, a mount, an item maker or a generic spell.
    assert(Listed(false, 30000, false, false, false, false) && Listed(false, 600000, false, false, false, false));
    assert(!Listed(false, 29999, false, false, false, false) && !Listed(true, 600000, false, false, false, false));
    assert(!Listed(false, 600000, true, false, false, false) && !Listed(false, 600000, false, true, false, false) &&
           !Listed(false, 600000, false, false, true, false));
    // Stuck (10 min) and Summon Friend (60 min): skill line GENERIC, never on the bar.
    assert(!Listed(false, 600000, false, false, false, true) && !Listed(false, 3600000, false, false, false, true));

    // The 7th field (3.1.3): the category of the addon's short bar.
    assert(Line(2825, 1500, 300000, true, "burst") == "KSPELL\tit\t2825\t1500\t300000\t1\tburst");
    assert(Line(29166, 0, 180000, false, "-") == "KSPELL\tit\t29166\t0\t180000\t0\t-");

    uint32_t id = 0;
    bool on = false;
    assert(ParseKeep("2825 on", id, on) && id == 2825 && on);
    assert(ParseKeep("29166 off ", id, on) && id == 29166 && !on);
    assert(!ParseKeep("2825", id, on) && !ParseKeep("abc on", id, on) && !ParseKeep("0 on", id, on) &&
           !ParseKeep("2825 maybe", id, on) && !ParseKeep("", id, on) && !ParseKeep("12345678901 on", id, on));
    return 0;
}
