/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA Live (session CoA SquidBot): every script of the session is registered here, in this order.
// Called once by AddPlayerbotsScripts (src/Script/Playerbots.cpp); a new script of the session goes here.

#include "CoaBotWhy.h"
#include "CoaDungeonClearLink.h"
#include "CoaLive.h"
#include "CoaMoveAudit.h"
#include "CoaPathProbe.h"
#include "CoaQuietAchievements.h"

void AddCoaLiveScripts()
{
    AddSC_coa_live();
    AddSC_coa_quiet_achievements();
    AddSC_coa_path_probe();
    AddSC_coa_move_audit();
    AddSC_coa_dungeon_clear_link();
    AddSC_coa_bot_why();
}
