#include "CoaSocialRules.h"

#include <cassert>

using namespace CoaSocial;

int main()
{
    BotView bot;
    bot.level = 30;
    PlayerView p;
    p.level = 31;
    p.distance = 10.0f;

    // Alone, out of a fight, close in level: the bot asks to group; grouped: a buff.
    assert(Decide(bot, p) == Act::Invite);
    p.inGroup = true;
    assert(Decide(bot, p) == Act::Buff);
    p.inGroup = false;
    p.level = 40;
    assert(Decide(bot, p) == Act::Buff);  // too far in level to group
    p.level = 31;
    BotView busy = bot;
    busy.inGroup = true;
    assert(Decide(busy, p) == Act::Buff);
    busy = bot;
    busy.inInstance = true;
    assert(Decide(busy, p) == Act::Buff);

    // Dead: raised, unless released or already offered.
    PlayerView dead = p;
    dead.alive = false;
    assert(Decide(bot, dead) == Act::Rez);
    dead.ghost = true;
    assert(Decide(bot, dead) == Act::None);
    dead.ghost = false;
    dead.resurrectAsked = true;
    assert(Decide(bot, dead) == Act::None);

    // A fight: help when it goes badly, never a buff or an invite in the middle of it.
    PlayerView fight = p;
    fight.inCombat = true;
    fight.healthPct = 80;
    fight.attackers = 1;
    assert(Decide(bot, fight) == Act::None);
    fight.healthPct = 35;
    assert(Decide(bot, fight) == Act::Help);
    fight.healthPct = 80;
    fight.attackers = 3;
    assert(Decide(bot, fight) == Act::Help);
    fight.level = 50;  // far above a damage dealer, but a healer still helps
    assert(Decide(bot, fight) == Act::None);
    BotView healer = bot;
    healer.canHeal = true;
    assert(Decide(healer, fight) == Act::Help);

    // Never the other faction, never too far.
    PlayerView other = p;
    other.sameFaction = false;
    assert(Decide(bot, other) == Act::None);
    other = p;
    other.distance = 31.0f;
    assert(Decide(bot, other) == Act::None);

    // Never asked to group in a city, a village, an inn or a sanctuary: a buff in passing there.
    PlayerView town = p;
    town.inSettlement = true;
    assert(Decide(bot, town) == Act::Buff);
    assert(IsSettlement(0x100, 0, false));        // capital
    assert(IsSettlement(0, 0x00200000, false));   // town with an inn
    assert(IsSettlement(0, 0x08, false));         // a district of a capital
    assert(IsSettlement(0, 0x800, false));        // sanctuary
    assert(IsSettlement(0, 0, true));             // resting (an inn anywhere)
    assert(!IsSettlement(0x40, 0x40, false));     // a forest where duels are allowed

    // Pace: one ask an hour per player by default (all bots together), raises and help immediate.
    assert(PlayerCooldownS(Act::Invite) == 3600 && PlayerCooldownS(Act::Invite, 600) == 600);
    assert(ChancePct(Act::Invite) == 15);
    assert(ChancePct(Act::Rez) == 100 && PlayerCooldownS(Act::None) == 0 && ChancePct(Act::None) == 0);
    return 0;
}
