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
    // The user's test in the Ascension channel (3.0.1), and close ways to say it.
    assert(AsksForGuild("looking for guild ?"));
    assert(AsksForGuild("looking for guild"));
    assert(AsksForGuild("Looking for a guild"));
    assert(AsksForGuild("lf guild"));
    assert(AsksForGuild("any guild recruiting?"));
    assert(AsksForGuild("lookin for guild"));
    assert(AsksForGuild("seeking a guild, lvl 30 hunter"));
    assert(AsksForGuild("searching for a guild"));
    assert(AsksForGuild("a la recherche d'une guilde"));
    assert(AsksForGuild("en quete d'une guilde"));

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

    // The channels: the world channel by name ("Ascension" on CoA, or the realm's own), not a private one.
    assert(PublicChannelName("Ascension", "Ascension"));
    assert(PublicChannelName("ascension", "World"));
    assert(PublicChannelName("World", "Ascension"));
    assert(PublicChannelName("MyRealmChat", "MyRealmChat"));
    assert(PublicChannelName("LookingForGroup", "World"));
    assert(!PublicChannelName("raidteam", "Ascension"));
    assert(!PublicChannelName("", ""));

    assert(Words("  Any, GUILD?? ") == " any guild ");
    assert(Words("qu'un") == " qu un ");

    static_assert(ANSWER_ASK_DEFAULT, "AnswerAsk is on by default");

    // A bot guild full of bots (RandomBotGuildSizeMax 15) still takes a player, up to the game's limit.
    assert(GuildHasPlace(15, 0));
    assert(GuildHasPlace(999, 0));
    assert(!GuildHasPlace(1000, 0));
    assert(GuildHasPlace(49, 50));
    assert(!GuildHasPlace(50, 50));

    // The "/w me" of the call in General: any whisper within 15 minutes, else words that ask.
    assert(WhisperAsks(1000, 1000 + 60, false));
    assert(WhisperAsks(1000, 1000 + 15 * 60, false));
    assert(!WhisperAsks(1000, 1000 + 15 * 60 + 1, false));
    assert(!WhisperAsks(0, 5000, false));
    assert(WhisperAsks(0, 5000, true));

    // One at a time, a rest per player.
    assert(MayAnswer(false, false, false, 0, 1000, 1800));
    assert(!MayAnswer(true, false, false, 0, 1000, 1800));
    assert(!MayAnswer(false, true, false, 0, 1000, 1800));
    assert(!MayAnswer(false, false, true, 0, 1000, 1800));
    assert(!MayAnswer(false, false, false, 1000, 2000, 1800));
    assert(MayAnswer(false, false, false, 1000, 2800, 1800));
    return 0;
}
