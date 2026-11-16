/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_DUNGEON_CLEAR_LINK_RULES_H
#define COA_DUNGEON_CLEAR_LINK_RULES_H

#include <cstdint>
#include <string>

// Whether Dungeon Clear is built into this server, said once in the log and to the SquidBots addon (CoA Bots 3.1.4).
// A server may build SquidBots without Dungeon Clear: the addon then says so instead of acting on nothing.
// Pure rules, tested alone.
namespace CoaDcLink
{
constexpr char const* PREFIX = "SQBW\t";
constexpr char const* CAPS_REQUEST = "SQBW\tCAPS?";
constexpr std::size_t VERSION_MAX = 32;
constexpr std::uint64_t CAPS_MIN_GAP_MS = 1000;  // one answer per player and second (protocol §0, rule 2)

// The addon's self-whisper asking what this server has.
inline bool IsCapsRequest(std::string const& msg) { return msg == CAPS_REQUEST; }

// A version as given by a module, made safe for a log line and an addon field: no tab or control character, at most
// 32 characters; empty: "-".
inline std::string CleanVersion(std::string const& version)
{
    std::string out;
    for (char c : version)
        if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F && out.size() < VERSION_MAX)
            out += c;
    return out.empty() ? "-" : out;
}

// The answer line, without the prefix: "CAPS\tbots=3.1.4\tdc=1\tdcver=3.1.3". Then "-" ends the answer.
// dcver is "-" when Dungeon Clear is absent, or built in by a version that does not announce itself.
inline std::string CapsLine(std::string const& botsVersion, bool dcPresent, std::string const& dcVersion)
{
    return "CAPS\tbots=" + CleanVersion(botsVersion) + "\tdc=" + (dcPresent ? "1" : "0") +
           "\tdcver=" + (dcPresent ? CleanVersion(dcVersion) : std::string("-"));
}

// The startup log line.
inline std::string LogLine(bool dcPresent, std::string const& dcVersion)
{
    if (!dcPresent)
        return "Dungeon Clear is not built into this server: the Dungeon features of SquidBots are unavailable.";
    std::string const version = CleanVersion(dcVersion);
    return version == "-" ? "Dungeon Clear is built into this server."
                          : "Dungeon Clear " + version + " is built into this server.";
}

// Dungeon Clear is there: it announced itself, or (a version that does not announce) it set its run provider.
inline bool Present(bool announced, bool runProviderSet) { return announced || runProviderSet; }

// One answer per player and second; lastMs is the player's last answer (0: none yet).
inline bool CapsAllowed(std::uint64_t lastMs, std::uint64_t nowMs)
{
    return !lastMs || nowMs - lastMs >= CAPS_MIN_GAP_MS;
}
}  // namespace CoaDcLink

#endif
