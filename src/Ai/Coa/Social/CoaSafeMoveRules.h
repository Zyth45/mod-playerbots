/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COASAFEMOVERULES_H
#define _PLAYERBOT_COASAFEMOVERULES_H

// A point move of the CoA code (city life, gathering, revive spot, out of water) goes only where the navmesh leads:
// the core moves a unit in a straight line, through walls and hills, when the path to the point is not found (a
// point inside a building, under a counter, off the mesh). Without the game objects, for the unit test.

#include <cmath>
#include <cstdint>

namespace CoaSafeMove
{
// The core's PathType flags (PathGenerator.h; checked against it in CoaSafeMove.cpp).
constexpr uint32_t NORMAL = 0x01, SHORTCUT = 0x02, INCOMPLETE = 0x04, NOPATH = 0x08, NOT_USING_PATH = 0x10,
                   SHORT = 0x20, FARFROMPOLY_START = 0x40, FARFROMPOLY_END = 0x80;

// A path the bot may walk: a whole path on the mesh to a point on it. The bot itself a little off the mesh is fine
// (FARFROMPOLY_START), a path cut short (SHORT, long trips) too. A straight line (NOT_USING_PATH: no mmap, or
// swimming) only for a swim in sight of the shore (waterLine: in water, the point in line of sight).
inline bool Walkable(uint32_t type, bool waterLine)
{
    if (!(type & NORMAL) || (type & (SHORTCUT | INCOMPLETE | NOPATH | FARFROMPOLY_END)))
        return false;
    return !(type & NOT_USING_PATH) || waterLine;
}
// 3.1.6 trial: in a capital, a random bot walked through walls. The generic move asks the core for a path, and the
// core goes in a straight line when it finds none (no path, or a path of two points). A random bot of its own (no
// real player's group), out of combat, on a normal move in a capital goes only where a whole path leads.
// What the core does with the path of a point move (PointMovementGenerator): it follows any path of more than two
// points that is not NOPATH (a cut path too: INCOMPLETE, the long ones cut at the point limit, or off the mesh at an
// end), and goes in a straight line otherwise. The straight line is the only thing refused in a capital; a path of two
// points is a straight line too, walkable only when it is a whole path on the mesh.
inline bool CoreStraightLine(uint32_t type, uint32_t points)
{
    if (type & NOPATH)
        return true;
    return points <= 2 && (!(type & NORMAL) || (type & (SHORTCUT | INCOMPLETE | NOT_USING_PATH | FARFROMPOLY_START |
                                                        FARFROMPOLY_END)));
}

// 3.1.8 (move audit of 20/11): a bot off a taxi stands at the height of the path's last node, up to a few yards over
// the ground (Morgan's Vigil: 2.9 yards), and the server never lets a still bot fall: it hung in the air for minutes.
// Dropped to its floor only in that exact case: outdoors (a platform or a tower has its own floor under the bot, an
// indoor floor is left alone), no water under it, a floor found, and between DROP_MIN and DROP_MAX above it.
constexpr float DROP_MIN = 1.5f;
constexpr float DROP_MAX = 8.0f;

inline bool DropsToFloor(float z, float floorZ, bool outdoors, bool waterBelow)
{
    if (floorZ <= -100000.0f || !outdoors || waterBelow)
        return false;
    float const above = z - floorZ;
    return above > DROP_MIN && above <= DROP_MAX;
}

// 3.1.8 (move audit of 20/11): a quarter of the level teleports go to a banker (ProbTeleToBankers), its position
// inside the bank, with the bot's home set there: 49 teleports of 44 bots into the Stormwind bank in 25 minutes. The
// arrival is now the first point outdoors around the banker, ring after ring (ARRIVAL_RINGS yards, 16 directions),
// on ground no more than ARRIVAL_MAX_DZ from the banker's floor; none found: the banker's spot, as before.
constexpr float ARRIVAL_RINGS[] = {10.0f, 15.0f, 20.0f, 25.0f, 30.0f, 35.0f, 40.0f};
constexpr uint32_t ARRIVAL_DIRECTIONS = 16;
constexpr float ARRIVAL_MAX_DZ = 6.0f;
// The bench after the fix (20/11): one arrival of the Stormwind bank was a roof by the door, 4.7 yards over the
// terrain. The arrival stands on the terrain itself: its ground no more than ARRIVAL_ON_TERRAIN over the map's grid.
constexpr float ARRIVAL_ON_TERRAIN = 1.0f;

inline bool ArrivalOk(float groundZ, float bankerZ, bool outdoors, bool water, float gridZ)
{
    return groundZ > -100000.0f && gridZ > -100000.0f && outdoors && !water &&
           std::fabs(groundZ - bankerZ) <= ARRIVAL_MAX_DZ && groundZ - gridZ <= ARRIVAL_ON_TERRAIN;
}

// 3.2 (the user's report of 20/11, bots standing on a ledge by the Stormwind bank, -8876.0 626.4 95.7): a path the
// core cuts short because the target is out of reach (another island of the mesh) ends on the reachable point
// nearest to it, here a ledge over the street with no way down; the bots walked there and stood. A path cut by the
// point limit (a long one, DEAD_END_LIMIT_POINTS points or more) is still walked: it goes on from its end. A short
// cut path whose end stays DEAD_END_MIN_GAP yards or more from the target is a dead end, refused in a capital.
constexpr uint32_t DEAD_END_LIMIT_POINTS = 70;
constexpr float DEAD_END_MIN_GAP = 5.0f;

inline bool DeadEndPath(uint32_t type, uint32_t points, float endToTarget)
{
    return (type & INCOMPLETE) && points < DEAD_END_LIMIT_POINTS && endToTarget >= DEAD_END_MIN_GAP;
}

// 3.2: an NPC behind a counter (a banker, an auctioneer): the stop picked by it lies over the counter, off the
// navmesh, and the path there is a dead end. The end of that path, the walkable point nearest the NPC, is taken
// instead when it is NPC_STAND_REACH yards or less from the NPC (the core lets a player talk from 5.5 yards).
constexpr float NPC_STAND_REACH = 4.5f;
// A wander that gave up this many NPCs in a row (no walkable way) ends: the bot goes on with something else
// instead of standing still and picking again.
constexpr uint32_t WANDER_GIVE_UPS = 3;

// 3.2 (living bench: 118 of 332 auctioneers given up on the way): a refused move is not always a refused way. The
// move layer also refuses a move to the very point of the last one within 5 seconds (StandByNpc's stop is the same
// each time), a bot that cannot move right now, a pull risk. An NPC is given up when it is gone, when no walkable
// stop by it exists, or after WANDER_REFUSALS refused moves toward it.
constexpr uint32_t WANDER_REFUSALS = 8;

enum class GiveUp { No, Gone, NoWay, Refusals };

inline GiveUp GivesUpNpc(bool gone, bool wayExists, uint32_t refusals)
{
    if (gone)
        return GiveUp::Gone;
    if (!wayExists)
        return GiveUp::NoWay;
    return refusals >= WANDER_REFUSALS ? GiveUp::Refusals : GiveUp::No;
}

// 3.2 (living bench, Undercity: a bot 46.2 yards from Auctioneer Yarly, 1.4 yards lower, got a straight line that
// ends 0.1 yard off; it was refused). A straight line in sight whose ground stays within FLAT_LINE_DZ yards of the
// line all along (sampled every FLAT_LINE_STEP yards: no canal, no ledge, no stairs) goes through no wall and over
// no fall. Accepted up to FLAT_LINE_MAX yards.
constexpr float FLAT_LINE_MAX = 60.0f;
constexpr float FLAT_LINE_DZ = 1.5f;
constexpr float FLAT_LINE_STEP = 2.0f;

inline bool FlatLineArrives(float length, float maxGroundGap, bool inSight)
{
    return inSight && length <= FLAT_LINE_MAX && maxGroundGap <= FLAT_LINE_DZ;
}

// 3.2 (living bench, Stormwind: from the street the path to all three auctioneers stops 16.1 yards short, their
// counter is cut off the street's navmesh). A dead end whose end stays BRIDGE_MAX yards or less from the NPC could be
// walked when the rest, from its end to the NPC, is a flat line. Not used in 3.2: the bench's bots stood at the end
// of it (StandByNpc); kept for the 3.2.1 fix (a fixed stop per Stormwind auctioneer).
constexpr float BRIDGE_MAX = 20.0f;

inline bool BridgesDeadEnd(float endToNpc, bool flatRest)
{
    return flatRest && endToNpc <= BRIDGE_MAX;
}

// 3.2 (BuyLog): why no walkable stop by an NPC exists, from the path to it.
inline char const* NoWayReason(uint32_t type, uint32_t points, bool closeEnough, bool inSight)
{
    if (type & FARFROMPOLY_START)
        return "bot off the mesh";
    if (type & NOPATH)
        return "no path";
    if (CoreStraightLine(type, points))
        return "straight line";
    if (!closeEnough)
        return "dead end";
    if (!inSight)
        return "out of sight";
    return "way found";
}

inline bool StandsByNpc(float endToNpc)
{
    return endToNpc <= NPC_STAND_REACH;
}

// 3.2.2 (living bench of d3bb1f81: the 3.2.1 lift changed nothing): under Auctioneer Chilton five floors of the navmesh
// lie within 2 yards of height, a closed island 0.1 yard under the floor the street reaches. Detour's findNearestPoly
// counts any polygon under the point within its climb height as distance 0, so a lift under that height ties and the
// island may still win. When the path to an NPC stops short, points on a ring around it are tried instead, where the
// floors no longer overlap: STAND_RING_DIRECTIONS directions, the first toward the bot, at each of STAND_RING_RADII.
// The first whose path ends by the NPC and in its sight is the stop, kept for that NPC.
constexpr uint32_t STAND_RING_DIRECTIONS = 8;
constexpr float STAND_RING_RADII[] = {2.0f, 3.5f};
constexpr uint32_t STAND_RING_TRIES = STAND_RING_DIRECTIONS * (sizeof(STAND_RING_RADII) / sizeof(STAND_RING_RADII[0]));
// 3.2.2 (navmesh read again: Chilton and Fitch stand on the closed island itself, the city's floor 1.57 yards lower,
// under the tiles' walkableClimb of 1.6, so a point at the NPC's height ties): each ring point is tried at the NPC's
// height, then STAND_RING_DROP yards lower, where only the floor below is within the climb.
constexpr float STAND_RING_DROP = 1.6f;
// 3.2.2 (the user's spots on the Stormwind trade square: its floor at z 94.3 to 94.7, about 3 yards under Chilton):
// and STAND_RING_DROP2 yards lower too.
constexpr float STAND_RING_DROP2 = 3.0f;
// 3.2.2 (sonde of a831bd0d, Undercity: Epitwee (8721) on her counter, the floor 5.3 yards under her): every ring point
// at these drops under the NPC's height, so that one is within the climb of the floor below whatever it is.
constexpr float STAND_RING_DROPS[] = {0.0f, STAND_RING_DROP, STAND_RING_DROP2, 4.5f, 6.0f};
constexpr uint32_t STAND_RING_DROP_COUNT = sizeof(STAND_RING_DROPS) / sizeof(STAND_RING_DROPS[0]);
// The core's own reach for an NPC (Player::GetNPCIfCanInteractWith: INTERACTION_DISTANCE 5.5 plus both objects'
// sizes, in 3D), half a yard short. A stop under a counter 5.3 yards high is within it, not within NPC_STAND_REACH.
constexpr float INTERACTION_REACH = 5.0f;

inline bool InInteractionReach(float dist, float npcSize, float botSize)
{
    return dist <= INTERACTION_REACH + npcSize + botSize;
}

// 3.2.2 (the user at the Ironforge bank: bots behind the counter, in the bankers' booths; his three spots in front of
// it, 4.1 to 4.8 yards out along each banker's facing): a stop by an NPC lies at least STAND_FRONT yards in front of
// it, along the way it faces at home. None found there: the stop found anywhere, as before (an NPC set facing a wall).
constexpr float STAND_FRONT = 1.0f;

inline bool InFrontOf(float npcX, float npcY, float facing, float x, float y)
{
    return (x - npcX) * std::cos(facing) + (y - npcY) * std::sin(facing) >= STAND_FRONT;
}

// 3.2.2 (the user's eye on the .go of 24/11): spots where the server's floor is not the one the players see, or that
// lie in a piece of the decor the server does not know. No arrival and no random step ends within radius yards of one
// (and AVOIDED_RISE yards of its height). Stops by an NPC are not concerned (Thunder Bluff's wind rider master).
struct AvoidedSpot
{
    uint32_t mapId;
    float x, y, z, radius;
};
constexpr float AVOIDED_RISE = 4.0f;
constexpr AvoidedSpot AVOIDED_SPOTS[] = {
    // Thunder Bluff, the wind riders' platform: the server's floor at 177.2-177.4, the client's at 180.5 (the user fell
    // from -1197.6 32.0 177.7 down the tower's spiral stairs, 3 or 4 floors). 3.2.3 (the user, 24/11: Tal's own spot,
    // -1196.75 26.08 177.6, is the true floor; the void lies in front of him): 4 yards around the void, not 12.
    {1, -1197.6f, 32.0f, 177.5f, 4.0f},
    // Thunder Bluff, a tent's pole the server does not know ("ce point me tp PILE dedans").
    {1, -1265.16f, 24.39f, 128.8f, 1.5f},
    // Stormwind, the bank's square: a tree's planter ("je suis sur le bac de l'arbre").
    {0, -8891.2f, 648.0f, 97.3f, 3.0f},
    // Stormwind, the bank: the bench and the ledge right of the bank's stairs ("sur le banc pas vraiment au sol", "un
    // peu sur un rebord", "un petit rebord a droite des escalier de la banque pas vraiment au sol"): -8893.8 643.9,
    // -8895.2 642.5 and -8889.8 643.9 in one disc. The hall's floor at -8897.6 640.0 (checked in game) stays out.
    {0, -8892.5f, 643.3f, 99.5f, 4.5f},
    // 3.2.3 (bench 3.3: a bot teleported to 9862.5 2342.8 1327.3, in the air over the server's floor at 1321.6): the
    // Darnassus spot by the auctioneers the user fell from ("je tombe d'environ la hauteur du personnage").
    {1, 9863.0f, 2344.2f, 1327.3f, 3.0f},
};

// 3.2.2 (the user: a bot in the Stormwind inn's floor after a random step): inside a capital, a random step ends only
// where the navmesh's floor and the server's agree within CITY_FLOOR_AGREE yards (a refused step: another sample).
constexpr float CITY_FLOOR_AGREE = 0.7f;
inline bool CityFloorAgrees(float meshZ, float serverZ) { return std::fabs(serverZ - meshZ) <= CITY_FLOOR_AGREE; }
// 3.2.2: a bot whose navmesh floor lies over it by more than CITY_FLOOR_AGREE yards is in the floor (CityUnstick).
inline bool BuriedUnder(float botZ, float meshZ) { return meshZ - botZ > CITY_FLOOR_AGREE; }
// 3.2.2 (CityUnstick): time still counts only while the bot's AI runs: a gap over STILL_SLEPT_MS is time asleep.
constexpr uint32_t STILL_SLEPT_MS = 30 * 1000;
inline uint32_t StillGain(uint32_t gapMs) { return gapMs > STILL_SLEPT_MS ? 0 : gapMs; }
constexpr uint32_t UNSTICK_EVERY_MS = 30 * 60 * 1000;  // a bot is moved out at most once in this time
constexpr uint32_t UNSTICK_PER_HOUR = 100;              // and the server moves at most this many an hour
constexpr uint32_t BURIED_EVERY_MS = 10 * 60 * 1000;    // a bot is checked for the floor at most this often
inline bool UnstickAllowed(uint32_t lastUnstickMs, uint32_t sinceLastMs, uint32_t thisHour)
{
    return (!lastUnstickMs || sinceLastMs >= UNSTICK_EVERY_MS) && thisHour < UNSTICK_PER_HOUR;
}

// 3.2.2 (Pilote, the user's eye): an arrival in one of these zones goes straight to a spot checked in game, instead
// of a search around it (the search fell back on the furniture around the Stormwind bank's stairs).
struct FixedArrivalSpot
{
    uint32_t mapId;
    float x, y, radius;  // the zone (2D, within AVOIDED_RISE yards of height of the spot)
    float fx, fy, fz;    // the spot
};
constexpr FixedArrivalSpot FIXED_ARRIVALS[] = {
    // Stormwind, the bank: the bench and the ledge right of the stairs; the hall's floor ("nickel dans la banque au sol
    // du bon cote public").
    {0, -8892.5f, 643.3f, 4.5f, -8897.6f, 640.0f, 99.7f},
};

inline bool FixedArrival(uint32_t mapId, float x, float y, float z, float& fx, float& fy, float& fz)
{
    for (FixedArrivalSpot const& spot : FIXED_ARRIVALS)
        if (spot.mapId == mapId && std::fabs(z - spot.fz) <= AVOIDED_RISE &&
            (x - spot.x) * (x - spot.x) + (y - spot.y) * (y - spot.y) <= spot.radius * spot.radius)
        {
            fx = spot.fx;
            fy = spot.fy;
            fz = spot.fz;
            return true;
        }
    return false;
}

// 3.2.3 (the user's eye, 24/11): NPCs whose front is a void the server does not know (Tal 2995, Thunder Bluff's wind
// rider master): a bot stops by their side, SIDE_STOP yards off them square to their facing (left, then right), on
// the server's floor within SIDE_STOP_RISE yards of the NPC's height.
constexpr uint32_t SIDE_STOP_NPCS[] = {2995};
constexpr float SIDE_STOP = 1.8f;
constexpr float SIDE_STOP_RISE = 1.0f;
inline bool SideStopNpc(uint32_t entry)
{
    for (uint32_t npc : SIDE_STOP_NPCS)
        if (npc == entry)
            return true;
    return false;
}
// The side point `side` (0 left, 1 right) of an NPC at (nx, ny) facing `facing`.
inline void SideStopPoint(float nx, float ny, float facing, int side, float& x, float& y)
{
    float const a = facing + (side == 0 ? 1.5707963f : -1.5707963f);
    x = nx + SIDE_STOP * std::cos(a);
    y = ny + SIDE_STOP * std::sin(a);
}
inline bool SideStopFloor(float npcZ, float floorZ) { return std::fabs(floorZ - npcZ) <= SIDE_STOP_RISE; }

// 3.2.3 (the user: bots riding inside inns): a bot indoors gets off its mount, walking or still (before: still only),
// out of a flight, a transport and a battleground (its tunnels read as indoors; a fight there is no stroll).
inline bool IndoorDismount(bool mounted, bool outdoors, bool inFlight, bool onTransport, bool inBattleground)
{
    return mounted && !outdoors && !inFlight && !onTransport && !inBattleground;
}

// 3.2.3 (player Agares216, 24/11: bots "cover" trainers, bankers, flight masters and mailboxes, taurens most): a bot
// stops by a still NPC or object off its axis, on an angle its guid fixes in an arc of the NPC's front, ASIDE_MIN_DEG
// to ASIDE_MIN_DEG + (ASIDE_STEPS - 1) * ASIDE_STEP_DEG off its facing, left or right; at the NPC's reach plus its own
// size plus ASIDE_GAP, and at least ASIDE_LATERAL yards off the axis; never past the wanted distance.
constexpr float ASIDE_MIN_DEG = 45.0f;
constexpr float ASIDE_STEP_DEG = 10.0f;
constexpr uint32_t ASIDE_STEPS = 4;
constexpr float ASIDE_GAP = 0.5f;
constexpr float ASIDE_LATERAL = 2.0f;
inline float AsideOffset(uint64_t guid)  // radians, signed (left +, right -)
{
    uint32_t const slot = uint32_t(guid % (2 * ASIDE_STEPS));
    float const deg = ASIDE_MIN_DEG + float(slot / 2) * ASIDE_STEP_DEG;
    return (slot % 2 ? -1.0f : 1.0f) * deg * 0.017453293f;
}
inline float AsideDistance(float npcReach, float botSize, float offset, float maxDistance)
{
    float d = npcReach + botSize + ASIDE_GAP;
    float const s = std::fabs(std::sin(offset));
    if (s > 0.01f && d * s < ASIDE_LATERAL)
        d = ASIDE_LATERAL / s;
    return maxDistance > 0.0f && d > maxDistance ? maxDistance : d;
}

inline bool Avoided(uint32_t mapId, float x, float y, float z)
{
    for (AvoidedSpot const& spot : AVOIDED_SPOTS)
        if (spot.mapId == mapId && std::fabs(z - spot.z) <= AVOIDED_RISE &&
            (x - spot.x) * (x - spot.x) + (y - spot.y) * (y - spot.y) <= spot.radius * spot.radius)
            return true;
    return false;
}

// 3.2.2 (the user's eye, Darnassus 9863.50 2345.68 1327.3: "je tombe d'environ la hauteur du personnage, juste en
// dessous du plafond", a bot by the auctioneers stood there, the server's ground 5 yards lower): a stop by an NPC lies
// on a floor the server sees, its height (GetHeight from just over it) within STOP_FLOOR_AGREE yards.
constexpr float STOP_FLOOR_AGREE = 1.5f;

inline bool OnServerFloor(float z, float ground)
{
    return ground > -100000.0f && std::fabs(ground - z) <= STOP_FLOOR_AGREE;
}

// 3.2.2 (Pilote, bench of 96b4114c: Timo online behind Ironforge's bankers 2460 and 2461, 2 yards behind each, while
// StandByNpc traced no stop behind them): a random step (MoveRandomNear: a wander, a nudge after a failed path) may end
// in a booth. A point within BOOTH_RANGE yards of a banker or an auctioneer, at most BOOTH_RISE yards above or under
// it, and not in front of it (InFrontOf), lies in its booth: no random step ends there.
constexpr float BOOTH_RANGE = 8.0f;
constexpr float BOOTH_RISE = 3.0f;

inline bool InBooth(float dist, float dz, bool inFront)
{
    return dist <= BOOTH_RANGE && std::fabs(dz) <= BOOTH_RISE && !inFront;
}

// 3.2.2 (sonde of 1e991b2d: the auctioneers of Ironforge and Orgrimmar, Stormwind's banker 137354, on a dais behind a
// counter: the path's end in front of them at 1.6 to 4.5 yards, "out of sight" at 1.5 yards up): the NPC is seen from
// the stop at 1.5 yards up or at the eyes (STAND_EYES, over a counter), or the stop is within NPC_STAND_REACH of it
// (the core asks no sight to talk, GetNPCIfCanInteractWith; the stop is in front of the NPC and on the path's floor).
// Unseen, the stop lies at most COUNTER_RISE yards above or under the NPC (Pilote: never another floor).
constexpr float STAND_EYES = 2.5f;
constexpr float COUNTER_RISE = 2.5f;

inline bool SeenOrAtCounter(bool seenLow, bool seenEyes, float dist, float dz)
{
    return seenLow || seenEyes || (dist <= NPC_STAND_REACH && std::fabs(dz) <= COUNTER_RISE);
}

// 3.2.2 (Pilote, 23/11, within the rule of credible moves): a bot standing on a closed island of the navmesh (its
// path to a stop known to be walkable stops short) gets off it: a walk in a straight line over checked flat ground
// to a point within UNSTICK_REACH yards from which the stop is walkable; else, when no real player sees it, a short
// teleport to the stop; else the visit is given up. NotStuck: the bot is not on an island.
constexpr float UNSTICK_REACH = 4.5f;
constexpr float UNSTICK_RADII[] = {1.5f, 3.0f, 4.5f};
constexpr uint32_t UNSTICK_DIRECTIONS = 8;
// NotStuck: its path reaches the known stop; NoKnownStop: no bot has stood by this NPC yet (3.2.2, Pilote: the bench
// of d061adec wrote no "off a navmesh island" line, the stops were known from a ring search only).
enum class Unstick { NotStuck, Walk, Teleport, Abandon, NoKnownStop };

// 3.2.2 (Pilote): a ring search that found nothing is not tried again for a while from the same start only (the
// bots teleported to the Stormwind bank failed it, and kept every other bot from trying for a minute). The start's
// cell: START_CELL yards a side.
constexpr float START_CELL = 4.0f;

inline uint32_t StartCell(float x, float y)
{
    int32_t const cx = int32_t(std::floor(x / START_CELL));
    int32_t const cy = int32_t(std::floor(y / START_CELL));
    return (uint32_t(cx) & 0xFFFFu) << 16 | (uint32_t(cy) & 0xFFFFu);
}

inline Unstick UnstickChoice(bool stuck, bool walkableNear, bool realPlayerInSight)
{
    if (!stuck)
        return Unstick::NotStuck;
    if (walkableNear)
        return Unstick::Walk;
    return realPlayerInSight ? Unstick::Abandon : Unstick::Teleport;
}

// The try'th point: its radius and its angle, from the angle toward the bot, alternating sides (0, +45, -45, +90...).
inline void StandRingPoint(uint32_t tryIndex, float towardBot, float& radius, float& angle)
{
    uint32_t const ring = tryIndex / STAND_RING_DIRECTIONS;
    uint32_t const step = tryIndex % STAND_RING_DIRECTIONS;
    radius = STAND_RING_RADII[ring < 2 ? ring : 1];
    float const turn = 6.2831853f / float(STAND_RING_DIRECTIONS);
    float const side = (step % 2) ? 1.0f : -1.0f;
    angle = towardBot + side * float((step + 1) / 2) * turn;
}

inline bool CityPathChecked(bool randomBot, bool realMaster, bool inCombat, bool generatePath, bool capital,
                            bool normalPriority)
{
    return randomBot && !realMaster && !inCombat && generatePath && capital && normalPriority;
}

// 3.2.3 (user in game, 24/11: a bot an hour under the Stormwind inn's floor, wandering to Human Commoner 18927): an NPC
// that walks by itself (waypoints, random moves) is no wander target: its spot changes, the path to it ends anywhere
// (under a floor, in a wall). The race commoners of the festivals walk the capitals' streets, whatever their spawn's
// movement says.
constexpr uint32_t WANDER_EXCLUDED_NPCS[] = {18927, 19148, 19169, 19171, 19172, 19173, 19175, 19176, 19177, 19178, 20102};

inline bool WanderNpcRefused(uint32_t entry, bool walksByItself)
{
    if (walksByItself)
        return true;
    for (uint32_t excluded : WANDER_EXCLUDED_NPCS)
        if (entry == excluded)
            return true;
    return false;
}
}  // namespace CoaSafeMove

#endif
