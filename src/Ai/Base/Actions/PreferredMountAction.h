/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_PREFERREDMOUNTACTION_H
#define PLAYERBOTS_PREFERREDMOUNTACTION_H

#include "Action.h"

class PlayerbotAI;

// CoA: "mount ?" lists the bot's mounts, "mount <spell>" makes one its preferred mount for its kind (ground or
// flying), "mount clear [ground|flying]" goes back to the automatic choice. No restart: the preferred mount cache
// and playerbots_preferred_mounts change together. The lines are read by SquidBots Lite (bot sheet).
class PreferredMountAction : public Action
{
public:
    PreferredMountAction(PlayerbotAI* botAI) : Action(botAI, "preferred mount") {}

    bool Execute(Event event) override;
};

#endif
