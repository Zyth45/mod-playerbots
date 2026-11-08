/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COALIVE_H
#define PLAYERBOTS_COALIVE_H

#include "Common.h"
#include "ObjectGuid.h"

#include <string>

class Player;
class PlayerbotAI;

// CoA Live (3.0, AiPlayerbot.CoaLive.Enable): random bots react like people around real players, with the lines and
// emotes of the bank of CoA IA MIND (playerbots_coa_live_*, CoaLiveRules.h). Every reaction is drawn, paced and
// typed with a human delay, then played by the world thread. Nothing runs, and nothing costs, when it is off.
namespace CoaLive
{
// The bank, from the playerbots database (at startup and on each config load). Empties the no-repeat rings.
void LoadBank();

// Every bot's update (map thread): about every 5 s, a random bot looks for a real player to greet, or idles at the
// inn or in a capital in front of one.
void OnBotUpdate(Player* bot, PlayerbotAI* botAI, uint32 diff);

// A real player whispers a random bot that is not its own: the bot answers from the bank, after a human delay.
// True when the whisper was taken (the bot then does not read it as a command).
bool OnWhisper(Player* from, Player* to, std::string const& msg);
// The old free chat replies leave such whispers to the bank (PlayerbotAI::HearChat).
bool AnswersWhispers(PlayerbotAI* botAI, ObjectGuid from);

// The bot accepted a resurrection cast by "by".
void OnResurrectAccepted(Player* bot, ObjectGuid by);
}  // namespace CoaLive

void AddSC_coa_live();

#endif
