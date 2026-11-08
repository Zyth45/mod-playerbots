/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAAUTONEXTQUEST_H
#define PLAYERBOTS_COAAUTONEXTQUEST_H

class Object;
class Player;
class Quest;

// A player who turns a quest in at a creature is handed its next quest at once when that one is auto-accept
// (QUEST_SPECIAL_FLAGS_AUTO_ACCEPT, e.g. the tauren Rites of the Earthmother 755, 757, 763): the core does it in
// WorldSession::HandleQuestgiverChooseRewardOpcode. Bots turn quests in with Player::RewardQuest, which skips that
// handler, so they never got those quests and every chain behind one stopped (767 Rite of Vision needs 763).
// The decision, pure (tested in tests/coa_auto_next_quest.cpp):
inline bool CoaTakesAutoNextQuest(bool givenByCreature, bool hasNext, bool autoAccept, bool canTake, bool canAdd)
{
    return givenByCreature && hasNext && autoAccept && canTake && canAdd;
}

// After bot->RewardQuest(quest, ..., questGiver): the same next quest as a player would get. Returns true when one
// was taken.
bool CoaTakeAutoNextQuest(Player* bot, Quest const* quest, Object* questGiver);

#endif
