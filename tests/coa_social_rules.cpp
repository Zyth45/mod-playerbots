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

    // Pace: invites are rare, raises and help immediate.
    assert(PlayerCooldownS(Act::Invite) == 1800 && ChancePct(Act::Invite) == 15);
    assert(ChancePct(Act::Rez) == 100 && PlayerCooldownS(Act::None) == 0 && ChancePct(Act::None) == 0);
    return 0;
}
