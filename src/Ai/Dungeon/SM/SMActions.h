/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_SMACTIONS_H
#define PLAYERBOTS_SMACTIONS_H

#include "MovementActions.h"

class ArcanistDoanMoveOutOfDetonationAction : public MovementAction
{
public:
    ArcanistDoanMoveOutOfDetonationAction(PlayerbotAI* botAI)
        : MovementAction(botAI, "arcanist doan move out of detonation") {}
    bool Execute(Event event) override;
};

#endif
