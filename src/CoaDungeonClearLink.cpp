/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaDungeonClearLink.h"

#include "CoaDungeonClearLinkRules.h"
#include "CoaGroupTank.h"
#include "GameTime.h"
#include "Log.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"

#include <atomic>
#include <mutex>
#include <unordered_map>

char const* CoaBotsVersion();  // Playerbots.cpp

namespace
{
// Written once at load (Announce), before the world updates; read afterwards.
std::string announcedVersion;
std::atomic<bool> announced{false};

std::mutex capsLock;
std::unordered_map<uint64, uint64> lastCapsMs;  // per player, under capsLock
}

namespace CoaDungeonClearLink
{
void Announce(char const* version)
{
    announcedVersion = version ? version : "";
    announced.store(true, std::memory_order_release);
}

bool Present()
{
    return CoaDcLink::Present(announced.load(std::memory_order_acquire), CoaGroupTank::HasProvider());
}

std::string Version()
{
    return announced.load(std::memory_order_acquire) ? announcedVersion : std::string();
}
}  // namespace CoaDungeonClearLink

namespace
{
// The log line, once: on the second world tick, after Dungeon Clear set its provider on its first one.
class CoaDungeonClearLinkWorldScript : public WorldScript
{
public:
    CoaDungeonClearLinkWorldScript() : WorldScript("CoaDungeonClearLinkWorldScript", {WORLDHOOK_ON_UPDATE}) {}

    void OnUpdate(uint32 /*diff*/) override
    {
        if (said || ++ticks < 2)
            return;
        said = true;
        LOG_INFO("server.loading", "SquidBots: {}",
                 CoaDcLink::LogLine(CoaDungeonClearLink::Present(), CoaDungeonClearLink::Version()));
    }

private:
    uint32 ticks = 0;
    bool said = false;
};

// "SQBW\tCAPS?" whispered (addon) by a player to itself: "SQBW\tCAPS\tbots=<v>\tdc=<0|1>\tdcver=<v>", then "SQBW\t-".
// AiPlayerbot.CoaCaps.Addon = 0: no answer, as on an older server (the addon then keeps its Dungeon features).
class CoaDungeonClearLinkPlayerScript : public PlayerScript
{
public:
    CoaDungeonClearLinkPlayerScript()
        : PlayerScript("CoaDungeonClearLinkPlayerScript", {PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT}) {}

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 lang, std::string& msg, Player* receiver) override
    {
        if (type != CHAT_MSG_WHISPER || lang != LANG_ADDON || !player || receiver != player ||
            !CoaDcLink::IsCapsRequest(msg) || !sPlayerbotAIConfig.coaCapsAddon)
            return true;

        uint64 const nowMs = GameTime::GetGameTimeMS().count();
        {
            std::lock_guard<std::mutex> guard(capsLock);
            uint64& last = lastCapsMs[player->GetGUID().GetRawValue()];
            if (!CoaDcLink::CapsAllowed(last, nowMs))
                return false;  // too many: dropped, no answer
            last = nowMs;
        }
        std::string const line = CoaDcLink::CapsLine(CoaBotsVersion(), CoaDungeonClearLink::Present(),
                                                     CoaDungeonClearLink::Version());
        player->Whisper(std::string(CoaDcLink::PREFIX) + line, LANG_ADDON, player);
        player->Whisper(std::string(CoaDcLink::PREFIX) + "-", LANG_ADDON, player);
        return false;
    }
};
}  // namespace

void AddSC_coa_dungeon_clear_link()
{
    new CoaDungeonClearLinkWorldScript();
    new CoaDungeonClearLinkPlayerScript();
}
