/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COATANKDIFFICULTYRULES_H
#define PLAYERBOTS_COATANKDIFFICULTYRULES_H

#include <cstdint>
#include <set>

// AiPlayerbot.CoaTankNormalDungeonOnly: tank specializations bots play in normal dungeons only, without the game
// objects, for the unit test.
namespace CoaTankDifficulty
{
// A raid, or a heroic (1) or mythic (2) dungeon: anything but a normal dungeon or the open world.
inline bool HardContent(bool raid, uint8_t difficulty)
{
    return raid || difficulty != 0;
}

// May a bot of this specialization tank there?
inline bool MayTank(std::set<uint32_t> const& normalOnly, uint32_t specializationId, bool hardContent)
{
    return !hardContent || !normalOnly.count(specializationId);
}
}  // namespace CoaTankDifficulty

#endif
