/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaHearthWithMaster.h"

#include "Group.h"
#include "Map.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Random.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"
#include "WorldSession.h"
#include <atomic>
#include <cmath>
#include <mutex>
#include <unordered_map>

namespace
{
constexpr uint32 HEARTHSTONE_ITEM = 6948;
constexpr uint32 HEARTHSTONE_SPELL = 8690;
constexpr float NEAR_MASTER = 30.0f;   // how close to the master a bot has to stand to hearth with him
constexpr float MASTER_MOVED = 40.0f;  // the master is this far from where he cast: he has left
constexpr uint32 GIVE_UP_MS = 60 * IN_MILLISECONDS;

enum class Phase : uint8
{
    Casting,    // the bot casts its hearthstone with its master
    Following,  // the master is gone: the bot follows him a moment later
};

struct State
{
    Phase phase;
    uint32 since;  // getMSTime() of the start
    uint32 mapId;  // where the master cast
    float x, y;
    uint32 goAt;  // Following: getMSTime() when the bot arrives
};

// Bots of several maps go through here: one lock, and a count so that most updates never take it.
std::mutex lock;
std::unordered_map<ObjectGuid, State> states;
std::atomic<uint32> pending{0};

bool SendsHome(SpellInfo const* info)
{
    if (!info)
        return false;
    for (SpellEffectInfo const& effect : info->Effects)
        if (effect.Effect == SPELL_EFFECT_TELEPORT_UNITS &&
            (effect.TargetA.GetTarget() == TARGET_DEST_HOME || effect.TargetB.GetTarget() == TARGET_DEST_HOME))
            return true;
    return false;
}

bool CastsHome(Player* player)
{
    Spell* spell = player->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    return spell && spell->getState() == SPELL_STATE_PREPARING && SendsHome(spell->GetSpellInfo());
}

bool CastsHearthstone(Player* bot)
{
    Spell* spell = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    return spell && spell->GetSpellInfo() && spell->GetSpellInfo()->Id == HEARTHSTONE_SPELL;
}

bool CanStart(Player* bot, Player* master)
{
    return bot->IsAlive() && !bot->IsInCombat() && !bot->InBattleground() && bot->IsInWorld() &&
           !bot->IsBeingTeleported() && bot->GetMapId() == master->GetMapId() &&
           bot->GetDistance(master) <= NEAR_MASTER && bot->GetGroup() && bot->GetGroup() == master->GetGroup() &&
           !bot->GetCurrentSpell(CURRENT_GENERIC_SPELL) && !bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL) &&
           !bot->HasSpellCooldown(HEARTHSTONE_SPELL) && bot->HasItemCount(HEARTHSTONE_ITEM, 1, false);
}

void Forget(ObjectGuid guid)
{
    std::lock_guard<std::mutex> guard(lock);
    if (states.erase(guid))
        pending.fetch_sub(1, std::memory_order_relaxed);
}

// The bot's hearthstone goes on cooldown, as after a cast that went through.
void SpendHearthstone(Player* bot)
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(HEARTHSTONE_SPELL);
    if (!info)
        return;
    Spell spell(bot, info, TRIGGERED_NONE);
    spell.SendSpellCooldown();
}

void JoinMaster(Player* bot, Player* master)
{
    float x = master->GetPositionX();
    float y = master->GetPositionY();
    float const z = master->GetPositionZ();
    float const angle = frand(0.0f, 2.0f * float(M_PI));
    float const distance = frand(2.0f, 4.0f);
    float const nx = x + std::cos(angle) * distance;
    float const ny = y + std::sin(angle) * distance;
    if (master->IsWithinLOS(nx, ny, z))
    {
        x = nx;
        y = ny;
    }
    bot->GetMotionMaster()->Clear();
    bot->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TELEPORTED | AURA_INTERRUPT_FLAG_CHANGE_MAP);
    bot->TeleportTo(master->GetMapId(), x, y, z, master->GetOrientation());
}
}  // namespace

void CoaHearthWithMaster::Tick(PlayerbotAI* botAI)
{
    if (!sPlayerbotAIConfig.coaHearthWithMaster || !botAI)
        return;
    Player* bot = botAI->GetBot();
    Player* master = botAI->GetMaster();
    if (!bot || !master || master == bot || !master->GetSession() || master->GetSession()->IsBot())
        return;

    ObjectGuid const guid = bot->GetGUID();
    State state{};
    bool known = false;
    if (pending.load(std::memory_order_relaxed))
    {
        std::lock_guard<std::mutex> guard(lock);
        auto const it = states.find(guid);
        if (it != states.end())
        {
            state = it->second;
            known = true;
        }
    }

    uint32 const now = getMSTime();
    if (!known)
    {
        if (!master->IsInWorld() || master->IsBeingTeleported() || !CastsHome(master) || !CanStart(bot, master))
            return;
        bot->StopMoving();
        if (bot->CastSpell(bot, HEARTHSTONE_SPELL, false) != SPELL_CAST_OK)
            return;
        std::lock_guard<std::mutex> guard(lock);
        if (states.emplace(guid, State{Phase::Casting, now, master->GetMapId(), master->GetPositionX(),
                                       master->GetPositionY(), 0})
                .second)
            pending.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    if (getMSTimeDiff(state.since, now) > GIVE_UP_MS || !bot->IsAlive())
    {
        if (CastsHearthstone(bot))
            bot->InterruptSpell(CURRENT_GENERIC_SPELL, false);
        Forget(guid);
        return;
    }

    if (state.phase == Phase::Casting)
    {
        bool const gone = master->IsBeingTeleported() || !master->IsInWorld() || master->GetMapId() != state.mapId ||
                          master->GetDistance2d(state.x, state.y) > MASTER_MOVED;
        if (gone)
        {
            if (!CastsHearthstone(bot))
            {
                // Its own cast went through first (a faster cast): the bot is home, nothing more to do.
                Forget(guid);
                return;
            }
            bot->InterruptSpell(CURRENT_GENERIC_SPELL, false);
            SpendHearthstone(bot);
            std::lock_guard<std::mutex> guard(lock);
            auto const it = states.find(guid);
            if (it != states.end())
            {
                it->second.phase = Phase::Following;
                it->second.goAt = now + urand(1000, 4000);
            }
            return;
        }
        // The master stopped his cast, or the bot's was broken: the bot stays.
        if (!CastsHome(master) || !CastsHearthstone(bot))
        {
            if (CastsHearthstone(bot))
                bot->InterruptSpell(CURRENT_GENERIC_SPELL, false);
            Forget(guid);
        }
        return;
    }

    // Following: once the master has arrived and the bot's moment has come.
    if (master->IsBeingTeleported() || !master->IsInWorld() || bot->IsBeingTeleported() ||
        getMSTimeDiff(state.since, now) < getMSTimeDiff(state.since, state.goAt))
        return;
    Forget(guid);
    if (bot->IsInCombat() || master->GetMap()->IsDungeon() || master->GetMap()->IsBattlegroundOrArena())
        return;
    JoinMaster(bot, master);
}
