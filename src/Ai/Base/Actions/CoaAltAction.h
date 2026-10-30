/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAALTACTION_H
#define PLAYERBOTS_COAALTACTION_H

#include "QuestAction.h"

class PlayerbotAI;

// "alt hello|quest <id>|take <id>|catchup <id>|train|learn all|learn <spell id>", whispered to an alt by its owner
// or sent by the SquidBots Lite addon (prefix SQBA, CoaAltAddon).
class CoaAltAction : public QuestAction
{
public:
    CoaAltAction(PlayerbotAI* botAI) : QuestAction(botAI, "alt") {}

    bool Execute(Event event) override;

protected:
    bool ProcessQuest(Quest const* /*quest*/, Object* /*questGiver*/) override { return false; }

private:
    bool Take(Quest const* quest, bool catchUp);
    void Refused(Quest const* quest, std::string const& state);
};

#endif
