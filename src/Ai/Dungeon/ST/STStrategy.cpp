/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "STStrategy.h"

void VanillaDungeonSTStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Avatar of Hakkar (STTriggers.h): suppressor first, then the flames, then the blood. Dungeon Clear ran them at
    // 64 / 63 / 62, above its own pull rungs (60); the same ladder here, above the stock raid actions.
    triggers.push_back(new TriggerNode("st hakkar suppressor", {NextAction("st hakkar suppressor", 64.0f)}));
    triggers.push_back(new TriggerNode("st hakkar flame", {NextAction("st hakkar flame", 63.0f)}));
    triggers.push_back(new TriggerNode("st hakkar blood", {NextAction("st hakkar blood", 62.0f)}));
}
