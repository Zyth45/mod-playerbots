/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA Combat: every script of the session is registered here, in this order.
// Called once by AddPlayerbotsScripts (src/Script/Playerbots.cpp); a new script of the session goes here.
// The session's strategies are registered in src/Bot/Engine/BuildSharedStrategyContexts.cpp
// (CoaCombatStrategyContext) and added to the bots in src/Bot/Factory/AiFactory.cpp.

#include "CoaBreathGuard.h"
#include "ScriptMgr.h"

namespace
{
    // The world-thread guards of the session, swept once a second.
    class CoaCombatWorldScript : public WorldScript
    {
    public:
        CoaCombatWorldScript() : WorldScript("CoaCombatWorldScript", {WORLDHOOK_ON_UPDATE}) {}

        void OnUpdate(uint32 diff) override
        {
            sinceSweepMs += diff;
            if (sinceSweepMs < SWEEP_MS)
                return;
            uint32 const elapsed = sinceSweepMs;
            sinceSweepMs = 0;
            CoaBreathGuard::Tick(elapsed);  // B2
        }

    private:
        static constexpr uint32 SWEEP_MS = 1000;
        uint32 sinceSweepMs = 0;
    };
}

void AddCoaCombatScripts()
{
    new CoaCombatWorldScript();
}
