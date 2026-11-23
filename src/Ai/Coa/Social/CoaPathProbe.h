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
// 3.2.2 (Pilote): what CoaSafeMove::StandByNpc answers from (x, y, z) for the NPC of that entry (its spawn nearest the
// point) or spawn id: the direct path's end, then the ring of stops (StandRingPoint), at the NPC's height and 1.6
// yards lower; the stop found and its distance to the NPC, or the failure. By Detour, with the bots' filter.
std::vector<std::string> StopProbe(uint32_t mapId, float x, float y, float z, uint32_t entryOrSpawn);
// The capitals' check: the lines of the pairs not joined, and a last line with the counts.
std::vector<std::string> CheckCapitals();

// 3.2.2 (Pilote: every teleport arrival in a capital on the floor the city walks on, not a fix for one bank). A
// teleport's destination, judged once and kept: Ok (on the city's floor, or not in a capital), Moved (x, y, z set to
// the nearest point that is, within 10 yards), Dropped (none: not a destination any more), Unknown (not judged yet: it
// goes as before, and is judged on the world thread soon). The city's floor: at least 600 polygons joined to the
// point's within 250 yards (the counters' NPCs stand on closed patches, no reference). AiPlayerbot.CoaArrivalCheck (1).
enum class Arrival { Unknown, Ok, Moved, Dropped };
Arrival ArrivalVerdict(uint32_t mapId, float& x, float& y, float& z);

// 3.2.2: the capitals' squares (CoaCityLifeRules' PLACE_*), found once per capital on the world thread and kept. The
// squares within range of (x, y) on that map, and the report of all of them (".squidbots places", the hourly line).
struct Place
{
    uint32_t id = 0;
    uint32_t mapId = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    std::string capital;
};
std::vector<Place> PlacesNear(uint32_t mapId, float x, float y, float range);
std::vector<std::string> PlacesReport();
// A bot settled on a square (City Life), counted for the hourly line.
void NotePlaceTaken();
}  // namespace CoaPathProbe

void AddSC_coa_path_probe();

#endif
