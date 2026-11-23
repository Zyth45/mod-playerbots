/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAPATHPROBE_H
#define PLAYERBOTS_COAPATHPROBE_H

#include <cstdint>
#include <string>
#include <vector>

class ChatHandler;

// CoA 3.2.2 (Pilote, from the d061adec bench): a probe of the navmesh with no player and no bot, from the console or
// RA. ".squidbots path <map> <x> <y> <z> <x> <y> <z>": the polygon each point snaps to (as the core's PathGenerator
// does, search box 3/5/3 then 3/50/3), whether the end polygon is reached from the start one (Detour's findPath: a
// partial result means another component of the navmesh), the straight path's points and its real end, how far it
// stops from the target, and every floor stacked under the target with whether it is reached. ".squidbots pathcheck":
// for each capital, from each arrival (a banker's point of TravelMgr: 6 yards in front, 2 higher; an innkeeper's, a
// flight master's) to each auctioneer, banker and flight master, one line per pair the navmesh does not join. With
// AiPlayerbot.CoaPathCheck = 1 (0 by default) the check also runs once, AiPlayerbot.CoaPathCheck.DelaySeconds after
// the start (the tiles load with the grids). Read only; nothing in the game changes.
namespace CoaPathProbe
{
// The probe's lines for one pair of points; empty on a map without a navmesh.
std::vector<std::string> Probe(uint32_t mapId, float fx, float fy, float fz, float tx, float ty, float tz);
// The capitals' check: the lines of the pairs not joined, and a last line with the counts.
std::vector<std::string> CheckCapitals();
}  // namespace CoaPathProbe

void AddSC_coa_path_probe();

#endif
