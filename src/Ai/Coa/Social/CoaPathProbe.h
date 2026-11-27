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
class Player;
class WorldObject;

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
// teleport's destination, judged once and kept: Ok (on the city's floor, or not in a capital), Moved (to the nearest
// point that is, within 10 yards), Dropped (none: not a destination any more), Unknown (not judged yet: judged on the
// world thread soon). z: the destination's own height going in (a banker's spot plus 2), in a capital the city's
// floor's height coming out (Ok and Moved set x, y, z: the vmap holds no floor under some buildings, the navmesh does). The city's floor: at least 600 polygons joined to the
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
// Why (x, y, z) is or is not a square: its floor, the services in range, the nearest of them, the ways, outdoors,
// flat, and the nearest square found (".squidbots place <map> x y z").
std::vector<std::string> PlaceExplain(uint32_t mapId, float x, float y, float z);
// 3.2.2: the ground under (x, y) from z: terrain, with the vmap, grid loaded or not, before and after loading it
// (".squidbots ground <map> x y z": whether the vmap holds a building's floor there).
std::vector<std::string> GroundProbe(uint32_t mapId, float x, float y, float z);
// 3.2.2 (Pilote): each capital's arrivals whose navmesh floor and server floor (GetHeight) differ by more than a yard:
// the list of places to check in game (".squidbots arrivals").
std::vector<std::string> ArrivalsDisagree();
// 3.2.2 (Pilote): a random bot of a capital standing still on a closed patch of the navmesh (fewer than 600 polygons
// joined within 250 yards: under a building, on a counter's stage) is put back on the city's floor nearby, when no
// real player sees it. Called on the bot's thread; at most once a minute per bot. True when it was moved.
bool RescueFromPatch(Player* bot);
// A bot settled on a square (City Life), counted for the hourly line.
void NotePlaceTaken();
// 3.2.2 (measure only, the user: bots standing on chairs): a City Life seat used (Use) or refused (SeatUsable) at
// arrival, and, while a bot holds a seat, its stand state (its latest per bot, for the hourly "seats" line).
void NoteSeatUse(bool used);
void NoteSeatHeld(Player* bot, WorldObject const* seat, int8_t use);
// 3.2.2: a seat holder found standing on its seat sat on it again (the "seats" line).
void NoteSeatSatAgain();
// 3.2.2 (the user: bots in the Stormwind inn's floor, a bot still for an hour there): a random bot of a capital (no
// fight, battleground or group with a player) in the floor (BuriedUnder: checked at its first look, then every
// BURIED_EVERY_MS), or still for StillMinutes on a wander (wandering: no target reached, no City Life post), is put on
// the capital's nearest checked arrival, even in a player's sight; at most once per UNSTICK_EVERY_MS per bot and
// UNSTICK_PER_HOUR in all. Counted in the hourly line. True when it was moved. Called on the bot's thread.
bool CityUnstick(Player* bot, bool wandering);
// 3.2.3 (AiPlayerbot.CoaLive.NpcStopAside, 1): a bot stops by a still NPC or object off its axis (MoveWorldObjectTo).
bool NpcStopAside();
}  // namespace CoaPathProbe

void AddSC_coa_path_probe();

#endif
