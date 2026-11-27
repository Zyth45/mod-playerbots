/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COACITYLIFERULES_H
#define PLAYERBOTS_COACITYLIFERULES_H

#include <cstdint>
#include <functional>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// CoA Live, L2 City Life (AiPlayerbot.CoaLive.City.Enable): a random bot on a city visit takes a "station" now and
// then (the inn, the bank, the auction house, an anvil or a forge, a training dummy), strikes a pose there and stays
// a few minutes, so that capitals look full. Pure rules: the stations, the pick, the stay, the poses.
namespace CoaCityLife
{
enum class Station : uint8_t
{
    None,
    Inn,      // an innkeeper: sits
    Bank,     // a banker: talks now and then
    Auction,  // an auctioneer: talks now and then
    Forge,    // an anvil or a forge: hammers
    Dummy,    // a training dummy: strikes it
    Beg,      // a beggar kneeling by a banker or an auctioneer
    Vendor,   // a buyer at a merchant
    Mail,     // a mailbox: reads its mail (3.1.6)
    Seat,     // a chair or a bench of the city: sits on it (3.1.6)
    Square,   // 3.2.2: a square of the city, with one or two others, sitting or talking
};

struct Candidate
{
    Station station = Station::None;
    uint64_t guid = 0;
    uint32_t botsThere = 0;  // bots already holding this station
};

// The station of a creature or object seen in a capital.
inline Station StationOfCreature(uint32_t npcFlags, std::string const& name)
{
    constexpr uint32_t VENDOR = 0x00000080, INNKEEPER = 0x00010000, BANKER = 0x00020000, AUCTIONEER = 0x00200000;
    if (npcFlags & INNKEEPER)
        return Station::Inn;
    if (npcFlags & BANKER)
        return Station::Bank;
    if (npcFlags & AUCTIONEER)
        return Station::Auction;
    if (name.find("Dummy") != std::string::npos)
        return Station::Dummy;
    if (npcFlags & VENDOR)
        return Station::Vendor;
    return Station::None;
}

// A spell focus game object: 1 anvil, 3 forge.
inline Station StationOfFocus(uint32_t focusId) { return focusId == 1 || focusId == 3 ? Station::Forge : Station::None; }

// A game object seen in a capital: a spell focus (StationOfFocus), a chair or bench (type 7), a mailbox (type 19).
inline Station StationOfObject(uint32_t goType, uint32_t focusId)
{
    constexpr uint32_t CHAIR = 7, SPELL_FOCUS = 8, MAILBOX = 19;
    if (goType == SPELL_FOCUS)
        return StationOfFocus(focusId);
    if (goType == CHAIR)
        return Station::Seat;
    if (goType == MAILBOX)
        return Station::Mail;
    return Station::None;
}

inline uint32_t Weight(Station s)
{
    switch (s)
    {
        case Station::Inn: return 30;
        case Station::Bank: return 20;
        case Station::Auction: return 20;
        case Station::Forge: return 15;
        case Station::Dummy: return 15;
        case Station::Beg: return 5;
        case Station::Vendor: return 15;
        case Station::Mail: return 10;
        case Station::Seat: return 10;
        case Station::Square: return 15;
        default: return 0;
    }
}

constexpr uint32_t MAX_BOTS_PER_STATION = 3;
constexpr uint32_t MAX_BEGGARS_PER_STATION = 1;  // one beggar at a door, never a queue of them

inline uint32_t MaxBots(Station s)
{
    if (s == Station::Beg || s == Station::Seat)  // one beggar at a door, one bot on a seat
        return 1;
    return s == Station::Mail ? 2 : MAX_BOTS_PER_STATION;
}
constexpr uint32_t CHANCE_PCT = 60;  // a pick of the next stop is a station rather than any NPC

// The station for the next stop: weighted by kind, never a full one (MaxBots), 0 when none fits. A bot does not
// take the same kind twice in a row (avoid: the kind of its last stop), unless nothing else fits.
// between(lo, hi) is an inclusive random draw.
inline uint64_t Pick(std::vector<Candidate> const& candidates, std::function<uint32_t(uint32_t, uint32_t)> const& between,
                     Station avoid = Station::None)
{
    auto weightOf = [avoid](Candidate const& c) -> uint32_t
    { return c.botsThere < MaxBots(c.station) && c.station != avoid ? Weight(c.station) : 0; };
    uint32_t total = 0;
    for (Candidate const& c : candidates)
        total += weightOf(c);
    if (!total)
        return avoid == Station::None ? 0 : Pick(candidates, between, Station::None);
    uint32_t roll = between(0, total - 1);
    for (Candidate const& c : candidates)
    {
        uint32_t const w = weightOf(c);
        if (!w)
            continue;
        if (roll < w)
            return c.guid;
        roll -= w;
    }
    return 0;
}

// How long a bot stays at a station (ms): a drink at the inn lasts, a word at the bank does not.
inline uint32_t StayMs(Station s, std::function<uint32_t(uint32_t, uint32_t)> const& between)
{
    switch (s)
    {
        case Station::Inn: return between(3 * 60, 8 * 60) * 1000;
        case Station::Forge:
        case Station::Dummy: return between(2 * 60, 5 * 60) * 1000;
        case Station::Bank:
        case Station::Auction:
        case Station::Vendor: return between(40, 120) * 1000;
        case Station::Beg: return between(2 * 60, 6 * 60) * 1000;
        case Station::Mail: return between(30, 90) * 1000;
        case Station::Seat: return between(3 * 60, 10 * 60) * 1000;
        case Station::Square: return between(2 * 60, 5 * 60) * 1000;
        default: return 0;
    }
}

// A long post (3.1.6): now and then a bot settles at the inn, a seat, a forge or a dummy for a long while, so
// that one sees the same faces at the same places. The kinds that can be one:
inline bool CanBePost(Station s)
{
    return s == Station::Inn || s == Station::Seat || s == Station::Forge || s == Station::Dummy;
}

// The stay at a station, with chancePct in a hundred of a long post of minMinutes to maxMinutes (in any order).
inline uint32_t StayOrPostMs(Station s, uint32_t chancePct, uint32_t minMinutes, uint32_t maxMinutes,
                             std::function<uint32_t(uint32_t, uint32_t)> const& between)
{
    if (CanBePost(s) && chancePct && maxMinutes && between(1, 100) <= chancePct)
    {
        uint32_t const lo = std::max<uint32_t>(std::min(minMinutes, maxMinutes), 1);
        uint32_t const hi = std::max(minMinutes, maxMinutes);
        return between(lo * 60, hi * 60) * 1000;
    }
    return StayMs(s, between);
}

// The one-shot emote played every few seconds while staying (0: none, the pose holds), and how often (ms).
struct Pose
{
    bool sit = false;
    uint32_t emote = 0;
    uint32_t everyMs = 0;
};

inline Pose PoseOf(Station s)
{
    constexpr uint32_t TALK = 1, BEG = 20, ATTACK_UNARMED = 35, ATTACK1H = 36;
    switch (s)
    {
        case Station::Inn: return {true, 0, 0};
        case Station::Bank:
        case Station::Auction:
        case Station::Vendor: return {false, TALK, 15000};
        case Station::Beg: return {false, BEG, 12000};
        case Station::Forge: return {false, ATTACK1H, 2500};
        case Station::Dummy: return {false, ATTACK_UNARMED, 1800};
        case Station::Seat: return {true, 0, 0};
        case Station::Square: return {false, TALK, 9000};
        default: return {};
    }
}
// The auction house: after a word with its auctioneer, the bot steps into the room, toward the middle of the
// auctioneers around it, with a little to the side, and stays there: they all stood at the counters along the
// walls, and nobody in the middle (3.0.1 in game). The middle: the mean of the auctioneers' positions.
constexpr float ROOM_STEP_MIN = 4.0f;
constexpr float ROOM_STEP_MAX = 10.0f;
constexpr float ROOM_SIDE = 3.0f;        // up to this far to either side
constexpr float ROOM_MIN_CENTER = 5.0f;  // a middle closer than this to the auctioneer: no room to step into
constexpr float ROOM_KEEP = 20.0f;       // a bot this close to its auctioneer still stays at its station

// The point `step` yards from the auctioneer (ox, oy) toward the middle (cx, cy), `side` yards to its left (negative:
// right), never past the middle. False when the middle is too close to the auctioneer.
inline bool IntoRoom(float ox, float oy, float cx, float cy, float step, float side, float& x, float& y)
{
    float const dx = cx - ox, dy = cy - oy;
    float const d = std::sqrt(dx * dx + dy * dy);
    if (d < ROOM_MIN_CENTER)
        return false;
    float const ux = dx / d, uy = dy / d;
    step = std::min(step, d);
    x = ox + ux * step - uy * side;
    y = oy + uy * step + ux * side;
    return true;
}

// The residents: a tenth of the random bots, always the same ones (by guid), stay three times as long on each
// city visit, so a capital keeps faces one sees again.
inline bool IsResident(uint64_t guid)
{
    uint64_t z = guid * 0x9E3779B97F4A7C15ull;
    z ^= z >> 29;
    return z % 10 == 0;
}

inline uint32_t StayMs(uint64_t guid, uint32_t cityStayMs) { return IsResident(guid) ? cityStayMs * 3 : cityStayMs; }
// 3.1.6 trial: using a chair puts the bot on the chair itself, wherever it stands. From the street, a chair of an
// upper floor, a balcony or a stall's roof within reach lifted the bot up there ("bots that fly"). A seat is taken only
// on the bot's own floor (height gap at most SEAT_MAX_DZ), close by and in sight; else the bot only stands by it.
constexpr float SEAT_PICK_MAX_DZ = 2.5f;  // a seat this far above or below the bot is not a candidate
constexpr float SEAT_MAX_DZ = 1.5f;       // at arrival: the chair's height against the bot's
constexpr float SEAT_MAX_DIST = 4.0f;     // at arrival: 2D distance to the chair
constexpr float SEAT_ON = 1.5f;           // 3.2.2: a bot this close stands on the chair (no sight asked)

inline bool SeatPickable(float botZ, float seatZ) { return std::fabs(seatZ - botZ) <= SEAT_PICK_MAX_DZ; }

inline bool SeatUsable(float botX, float botY, float botZ, float seatX, float seatY, float seatZ, bool inSight)
{
    float const dx = seatX - botX, dy = seatY - botY;
    // 3.2.2 (the user: bots standing on chairs): a bot whose stop is the chair itself stands in its model, which hides
    // the chair from it: on it (SEAT_ON yards), no sight is asked.
    bool const onIt = dx * dx + dy * dy <= SEAT_ON * SEAT_ON;
    return (inSight || onIt) && std::fabs(seatZ - botZ) <= SEAT_MAX_DZ && dx * dx + dy * dy <= SEAT_MAX_DIST * SEAT_MAX_DIST;
}
// 3.2.2 (the user, 23/11: bots standing on the capitals' squares, by the auction house's door and the quest board, on
// the square between the bank and the auction house; "on est pas préssé ... essai de tout faire pour la 3.2.2"). A
// square: an open space (PlaceOpen), on the city's floor, flat, within PLACE_SERVICE_MAX yards of at least two services of different
// kinds (an auctioneer, a banker, an innkeeper, a flight master; a quest board, a mailbox), never closer than
// PLACE_NPC_MIN to an NPC of them (its door, its counter) or PLACE_OBJECT_MIN to an object, off the ways between them
// (refused until 3.2.2, now allowed). At most PLACES_PER_CAPITAL per capital, PLACE_SPACING yards apart. The user's own spots on
// the Stormwind trade square (23/11, 6 of them, 7 to 60 yards apart, some by the quest board and the mailbox) set these.
// 3.2.2 (sonde of 603a6549, the user's 31 spots: 17 kept; 8 refused for "no service within 50 yards", 5 "on a way", 1
// "too close": spots chosen by eye, by a fountain, a quest board, a flight master): a square needs no service around
// (the services only rank the squares), only standing right on a way's line is refused, and 2 yards from any NPC.
constexpr float PLACE_NPC_MIN = 2.0f;
constexpr float PLACE_OBJECT_MIN = 2.0f;
constexpr float PLACE_SERVICE_MAX = 50.0f;
// 3.2.2 (sonde of 2448cbcb, the user's 31 spots: 15 refused for "fewer than two kinds", 6 "on a way"): one kind of
// service in range is enough. (sonde of 5f662017: the last 5 refused were on a way: no longer a refusal.)
constexpr float PLACE_SPACING = 7.0f;
constexpr float PLACE_GRID = 3.0f;
constexpr float PLACE_FLAT_DZ = 0.6f;  // the ground 2 yards around stays this close in height
constexpr uint32_t PLACES_PER_CAPITAL = 12;
constexpr uint32_t PLACE_OBJECT_KINDS = 16 | 32;  // the kinds that are objects: a quest board, a mailbox
// 3.2.2 (the user's spots: Ironforge and Undercity are indoors, so "outdoors" refused them): an open space instead. From
// the head (PLACE_HEAD yards up), at least PLACE_OPEN_MIN of PLACE_OPEN_DIRECTIONS directions free of walls for
// PLACE_OPEN_REACH yards, and nothing overhead for PLACE_CEILING yards above the head.
constexpr float PLACE_HEAD = 1.5f;
constexpr float PLACE_OPEN_REACH = 4.0f;
constexpr uint32_t PLACE_OPEN_DIRECTIONS = 8;
constexpr uint32_t PLACE_OPEN_MIN = 6;
constexpr float PLACE_CEILING = 2.5f;

inline bool PlaceOpen(uint32_t openDirections, bool ceilingClear)
{
    return ceilingClear && openDirections >= PLACE_OPEN_MIN;
}

// A candidate's kinds of services in range (a bit per kind): a square when at least one.
inline bool PlaceServes(uint32_t kindsInRange)
{
    return kindsInRange != 0;
}

// The distance from (px, py) to the segment (ax, ay)-(bx, by), 2D.
inline float SegmentDistance(float px, float py, float ax, float ay, float bx, float by)
{
    float const dx = bx - ax, dy = by - ay;
    float const len2 = dx * dx + dy * dy;
    float t = len2 > 0.0f ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0f;
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    float const ex = ax + t * dx - px, ey = ay + t * dy - py;
    return std::sqrt(ex * ex + ey * ey);
}

// The bots of a square stand on a small circle, facing its middle: the slot'th of up to three, PLACE_SLOT_RADIUS
// yards out, a third of a turn apart.
constexpr float PLACE_SLOT_RADIUS = 1.3f;
constexpr float PLACE_ARRIVED = 1.5f;     // a bot this close to its slot stands there
constexpr uint32_t PLACE_SIT_PCT = 40;    // of the bots settled on a square, this many sit

inline void PlaceSlot(uint32_t slot, float cx, float cy, float& x, float& y)
{
    float const angle = float(slot % 3) * 2.0943951f;
    x = cx + PLACE_SLOT_RADIUS * std::cos(angle);
    y = cy + PLACE_SLOT_RADIUS * std::sin(angle);
}
}  // namespace CoaCityLife

#endif
