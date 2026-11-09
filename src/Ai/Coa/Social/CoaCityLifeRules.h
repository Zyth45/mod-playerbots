/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COACITYLIFERULES_H
#define PLAYERBOTS_COACITYLIFERULES_H

#include <cstdint>
#include <functional>
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
        default: return 0;
    }
}

constexpr uint32_t MAX_BOTS_PER_STATION = 3;
constexpr uint32_t MAX_BEGGARS_PER_STATION = 1;  // one beggar at a door, never a queue of them

inline uint32_t MaxBots(Station s) { return s == Station::Beg ? MAX_BEGGARS_PER_STATION : MAX_BOTS_PER_STATION; }
constexpr uint32_t CHANCE_PCT = 60;  // a pick of the next stop is a station rather than any NPC

// The station for the next stop: weighted by kind, never a full one (MAX_BOTS_PER_STATION), 0 when none fits.
// between(lo, hi) is an inclusive random draw.
inline uint64_t Pick(std::vector<Candidate> const& candidates, std::function<uint32_t(uint32_t, uint32_t)> const& between)
{
    uint32_t total = 0;
    for (Candidate const& c : candidates)
        if (c.botsThere < MaxBots(c.station))
            total += Weight(c.station);
    if (!total)
        return 0;
    uint32_t roll = between(0, total - 1);
    for (Candidate const& c : candidates)
    {
        if (c.botsThere >= MaxBots(c.station))
            continue;
        uint32_t const w = Weight(c.station);
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
        default: return 0;
    }
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
        default: return {};
    }
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
}  // namespace CoaCityLife

#endif
