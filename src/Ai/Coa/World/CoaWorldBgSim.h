/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA World: simulated battlegrounds (CoaWorldBgSim.cpp).

#ifndef PLAYERBOTS_COAWORLDBGSIM_H
#define PLAYERBOTS_COAWORLDBGSIM_H

// For RandomPlayerbotMgr: AiPlayerbot.CoaWorld.BgSim.ReplaceAutoJoin (default 1). True: the bots do not start
// battlegrounds and arenas by themselves (RandomBotAutoJoinBG), whatever that key says; they fill real players' queues.
bool CoaWorldBgSimReplacesAutoJoin();

void AddSC_coa_world_bg_sim();

#endif
