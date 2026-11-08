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
#include <unordered_map>

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

// Sessions (AiPlayerbot.CoaLive.Sessions.Enable, RandomPlayerbotMgr): the random bots wanted now, from the bot count
// (the count itself when off); a session in the world and a break away, in seconds (0 when off: the old values).
uint32 SessionTarget(uint32 count);
uint32 SessionSeconds();
uint32 OfflineSeconds();
bool SessionsEnabled();

// For the other sessions (CoA World...): their own situations of the bank, said the CoA Live way. Both cost nothing
// and give nothing when AiPlayerbot.CoaLive.Enable is 0 (the caller keeps its own text).
// SayLine: the bot draws a line of the situation (rules, chance, soul, no-repeat), filled with the bot's values and
// the slots ("city" fills %city), and says it on the line's channel after a human typing delay. False when the rule
// or the chance says no, or the bank has nothing to say. Any thread; played by the world thread.
bool SayLine(Player* bot, std::string const& situation, std::unordered_map<std::string, std::string> const& slots);
// SystemLine: a line of the situation in the reader's language (English when it has none), filled with the slots
// only, for a system announce: no pace, no typo, no slang. Empty when there is none or a slot is missing.
std::string SystemLine(Player* reader, std::string const& situation,
                       std::unordered_map<std::string, std::string> const& slots);
}  // namespace CoaLive

void AddSC_coa_live();

#endif
