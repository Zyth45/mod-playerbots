#include "CoaWorldGuildAskRules.h"

#include <cassert>

using namespace CoaWorldGuildAskRules;

int main()
{
    // What players wrote on Discord after the 3.0, and the usual ways to ask.
    assert(AsksForGuild("I did ask if anyone had a guild I could join"));
    assert(AsksForGuild("anyone have a guild I could join?"));
    assert(AsksForGuild("guild request"));
    assert(AsksForGuild("LF guild"));
    assert(AsksForGuild("LFG guild, leveling a mage"));
    assert(AsksForGuild("lf a guild pls"));
    assert(AsksForGuild("any guild?"));
    assert(AsksForGuild("Any guilds recruiting"));
    assert(AsksForGuild("looking for a guild"));
    assert(AsksForGuild("Looking for an active guild!"));
    assert(AsksForGuild("need a guild"));
    assert(AsksForGuild("guild?"));
    assert(AsksForGuild("a guild ?"));
    assert(AsksForGuild("ginv pls"));
    assert(AsksForGuild("can I join your guild"));
    assert(AsksForGuild("invite me to a guild please"));
    assert(AsksForGuild("guild inv?"));
    assert(AsksForGuild("cherche une guilde"));
    assert(AsksForGuild("guilde ?"));
    assert(AsksForGuild("une guilde ?"));
    assert(AsksForGuild("quelqu'un a une guilde ?"));
    assert(AsksForGuild("Je cherche guilde pour monter"));
    assert(AsksForGuild("une guilde qui recrute ?"));
    assert(AsksForGuild("inv guilde svp"));
    assert(AsksForGuild("je voudrais rejoindre une guilde"));

    // Not a request.
    assert(!AsksForGuild("<Iron Wolves> is recruiting, /w me"));
    assert(!AsksForGuild("Iron Wolves is recruiting all levels"));
    assert(!AsksForGuild("join my guild, we raid on friday"));
    assert(!AsksForGuild("our guild is recruiting"));
    assert(!AsksForGuild("ma guilde recrute"));
    assert(!AsksForGuild("on recrute dans la guilde"));
    assert(!AsksForGuild("what guild?"));
    assert(!AsksForGuild("which guild is that?"));
    assert(!AsksForGuild("quelle guilde ?"));
    assert(!AsksForGuild("my guild left me"));
    assert(!AsksForGuild("LFG Deadmines"));
    assert(!AsksForGuild("anyone selling linen?"));
    assert(!AsksForGuild("the guild bank is nice"));
    assert(!AsksForGuild(""));
    assert(!AsksForGuild("guildmaster in Stormwind?"));

    assert(Words("  Any, GUILD?? ") == " any guild ");
    assert(Words("qu'un") == " qu un ");

    // One at a time, a rest per player.
    assert(MayAnswer(false, false, false, 0, 1000, 1800));
    assert(!MayAnswer(true, false, false, 0, 1000, 1800));
    assert(!MayAnswer(false, true, false, 0, 1000, 1800));
    assert(!MayAnswer(false, false, true, 0, 1000, 1800));
    assert(!MayAnswer(false, false, false, 1000, 2000, 1800));
    assert(MayAnswer(false, false, false, 1000, 2800, 1800));
    return 0;
}
