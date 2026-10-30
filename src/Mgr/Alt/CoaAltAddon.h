/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAALTADDON_H
#define PLAYERBOTS_COAALTADDON_H

#include "Common.h"

#include <string>
#include <vector>

class Player;
class PlayerbotAI;
class Quest;

// The alts and the SquidBots Lite addon (contract CONTRAT-ALTS-2.8.3): addon messages of prefix "SQBA",
// whispered by the alt to its owner, and the commands the addon whispers back ("alt <command>").
namespace CoaAltAddon
{
constexpr uint32 PROTOCOL = 1;

struct QuestState
{
    std::string state;
    std::string detail;
    bool catchUp = false;
};

// An addon whisper of the owner to one of its alts. False when it was no "SQBA" message.
bool OnAddonWhisper(Player* from, Player* receiver, std::string const& msg);

void MarkSeen(Player* master);
bool MasterHasAddon(Player* master);

// The owner has the addon: the answers of the "alt" command go to it.
bool AddonSeen(PlayerbotAI* botAI);
// AltChatToAddon and the owner has the addon: the repetitive quest and trainer lines go to it, not to the chat.
bool ToAddon(PlayerbotAI* botAI);

void Send(Player* bot, Player* to, std::string const& payload);
void SendToMaster(PlayerbotAI* botAI, std::string const& payload);
std::string Field(std::string const& text, size_t max = 60);

QuestState StateOf(Player* bot, Quest const* quest);
void SendQuestState(PlayerbotAI* botAI, Quest const* quest);

// The prerequisites to mark rewarded so that the quest can be taken, the oldest first. False when the quest is
// blocked by something else than its chain, or when the chain cannot be caught up (another race, a daily...).
bool PlanCatchUp(Player* bot, Quest const* quest, std::vector<uint32>& plan);
// AiPlayerbot.CatchUpQuestChains, alts only: marks the missing prerequisites rewarded. Returns how many.
uint32 CatchUp(PlayerbotAI* botAI, Quest const* quest);
}  // namespace CoaAltAddon

#endif
