/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaQuickAsk.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "AiObjectContext.h"
#include "CoaAltAddon.h"
#include "CoaPlayerMainTank.h"
#include "CoaTankAction.h"
#include "Creature.h"
#include "GameTime.h"
#include "Group.h"
#include "Lust/CoaBossLustRule.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

namespace CoaQuickAsk
{
namespace
{
constexpr uint8 MOON = 4;        // server index of the moon, the crowd control mark the bots leave alone
constexpr uint8 NO_ICON = 0xFF;  // a taunted target pinned without a raid icon
constexpr size_t MAX_ASKS = 64;

// Under askLock. Written by the chat hooks (world thread) and the bots' queues, read by the bots' updates.
struct Ask
{
    uint32 id = 0;
    Kind kind = Kind::None;
    Origin origin = Origin::Party;
    ObjectGuid player;
    ObjectGuid target;  // as asked: the addon's guid or the player's selection (empty: none)
    uint64 heardMs = 0;
    bool claimed = false;   // cc, taunt: a bot is trying it (its cast is outside the lock)
    bool done = false;      // nothing more to do
    bool anyCast = false;   // heal: a heal was cast
    bool answered = false;  // the player has its answer
    Reason reason = Reason::NoBot;  // the strongest reason heard from the bots that could not
};

std::mutex askLock;
std::vector<Ask> asks;
std::unordered_map<uint64, uint64> lastAsk;  // player * 4 + kind -> last ask (ms)
std::atomic<bool> anyAsk{false};
uint32 nextId = 1;

uint64 NowMs() { return GameTime::GetGameTimeMS().count(); }

bool KindOn(Kind kind)
{
    if (!sPlayerbotAIConfig.coaLiveEnable)
        return false;
    switch (kind)
    {
        case Kind::Heal:
            return sPlayerbotAIConfig.coaLiveHealRequest;
        case Kind::Cc:
            return sPlayerbotAIConfig.coaLiveCcRequest;
        case Kind::Taunt:
            return sPlayerbotAIConfig.coaLiveTauntRequest;
        default:
            return false;
    }
}

bool Hear(Player* player, Kind kind, ObjectGuid target, Origin origin)
{
    uint64 const nowMs = NowMs();
    uint64 const key = player->GetGUID().GetRawValue() * 4 + uint64(kind);
    std::lock_guard<std::mutex> guard(askLock);
    auto const last = lastAsk.find(key);
    if (!MayAsk(last == lastAsk.end() ? 0 : last->second, nowMs) || asks.size() >= MAX_ASKS)
        return false;
    lastAsk[key] = nowMs;
    if (lastAsk.size() > 20000)
        lastAsk.clear();
    // A new ask of the same kind replaces the player's older one (a new target).
    ObjectGuid const asker = player->GetGUID();
    asks.erase(std::remove_if(asks.begin(), asks.end(),
                              [&](Ask const& a) { return a.player == asker && a.kind == kind; }),
               asks.end());
    Ask ask;
    ask.id = nextId++;
    ask.kind = kind;
    ask.origin = origin;
    ask.player = asker;
    ask.target = target;
    ask.heardMs = nowMs;
    asks.push_back(ask);
    anyAsk = true;
    return true;
}

Unit* UnitOf(Player* bot, ObjectGuid guid)
{
    if (!guid || !bot->IsInWorld() || !bot->FindMap())
        return nullptr;
    return ObjectAccessor::GetUnit(*bot, guid);
}

// The bot knows a higher rank of this spell: only the highest is used.
bool LowerRank(Player* bot, uint32 spellId)
{
    uint32 const next = sSpellMgr->GetNextSpellInChain(spellId);
    return next && bot->HasSpell(next);
}

uint32 CdTotal(SpellInfo const* info) { return std::max(info->RecoveryTime, info->CategoryRecoveryTime); }

constexpr uint32 CC_MECHANICS = (1u << MECHANIC_POLYMORPH) | (1u << MECHANIC_BANISH) | (1u << MECHANIC_SAPPED) |
                                (1u << MECHANIC_SHACKLE) | (1u << MECHANIC_SLEEP);

bool IsCcSpell(SpellInfo const* info)
{
    bool ccAura = info->Mechanic < 32 && ((1u << info->Mechanic) & CC_MECHANICS);
    bool enemy = false;
    for (SpellEffectInfo const& effect : info->Effects)
    {
        if (!effect.Effect)
            continue;
        if (effect.TargetA.GetCheckType() == TARGET_CHECK_ENEMY)
            enemy = true;
        if (effect.IsAura() && (effect.ApplyAuraName == SPELL_AURA_MOD_CONFUSE ||
                                effect.ApplyAuraName == SPELL_AURA_MOD_FEAR || effect.ApplyAuraName == SPELL_AURA_MOD_STUN))
            ccAura = true;
        if (effect.Mechanic < 32 && ((1u << effect.Mechanic) & CC_MECHANICS))
            ccAura = true;
    }
    return IsCrowdControl(info->IsPassive(), info->IsAffectingArea(), enemy && info->NeedsExplicitUnitTarget(), ccAura,
                          info->GetMaxDuration());
}

// A boss is never crowd controlled; a target already under one is left to it.
bool BossLike(Unit* target)
{
    Creature* creature = target->ToCreature();
    return creature && (creature->IsDungeonBoss() || creature->isWorldBoss());
}

bool UnderCc(Unit* target)
{
    return target->HasAuraType(SPELL_AURA_MOD_CONFUSE) || target->HasAuraType(SPELL_AURA_MOD_FEAR) ||
           target->HasAuraType(SPELL_AURA_MOD_STUN) || target->HasAuraWithMechanic(CC_MECHANICS);
}

bool IsDefensive(SpellInfo const* info, uint8 depth = 0)
{
    for (SpellEffectInfo const& effect : info->Effects)
    {
        if (effect.IsAura() && effect.TargetA.GetCheckType() != TARGET_CHECK_ENEMY)
            switch (effect.ApplyAuraName)
            {
                case SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN:
                    if (effect.BasePoints < 0)
                        return true;
                    break;
                case SPELL_AURA_SCHOOL_IMMUNITY:
                case SPELL_AURA_DAMAGE_IMMUNITY:
                case SPELL_AURA_SCHOOL_ABSORB:
                case SPELL_AURA_MOD_INCREASE_HEALTH:
                case SPELL_AURA_MOD_INCREASE_HEALTH_PERCENT:
                case SPELL_AURA_SPLIT_DAMAGE_PCT:
                    return true;
                default:
                    break;
            }
        if (depth < 1 && effect.TriggerSpell && effect.TriggerSpell != info->Id)
            if (SpellInfo const* triggered = sSpellMgr->GetSpellInfo(effect.TriggerSpell))
                if (IsDefensive(triggered, depth + 1))
                    return true;
    }
    return false;
}

// The heal the bot casts on `target` now: a single-target heal it knows (cooldown HEAL_MAX_CD_MS at most), ready, in
// range and sight, a fast direct heal first. 0 and why when none.
uint32 HealFor(Player* bot, PlayerbotAI* botAI, Unit* target, Reason& why)
{
    std::vector<CoaHealRequest::HealSpell> spells;
    bool any = false, inRange = false, cd = false;
    bool const sight = bot->IsWithinLOSInMap(target);
    for (auto const& [spellId, known] : bot->GetSpellMap())
    {
        if (!known || known->State == PLAYERSPELL_REMOVED || !known->Active)
            continue;
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
        if (!info || info->IsPassive() || CdTotal(info) > HEAL_MAX_CD_MS)
            continue;
        bool direct = false, overTime = false, single = false;
        for (SpellEffectInfo const& effect : info->Effects)
        {
            if (effect.Effect == SPELL_EFFECT_HEAL)
                direct = true;
            else if (effect.Effect == SPELL_EFFECT_APPLY_AURA && effect.ApplyAuraName == SPELL_AURA_PERIODIC_HEAL)
                overTime = true;
            else
                continue;
            uint32 const t = effect.TargetA.GetTarget();
            if (t == TARGET_UNIT_TARGET_ALLY || t == TARGET_UNIT_TARGET_PARTY || t == TARGET_UNIT_TARGET_RAID ||
                t == TARGET_UNIT_TARGET_ANY)
                single = true;
        }
        if (!(direct || overTime) || !single)
            continue;
        any = true;
        if (!sight || !bot->IsWithinDistInMap(target, info->GetMaxRange(true, bot)))
            continue;
        inRange = true;
        if (bot->HasSpellCooldown(spellId))
        {
            cd = true;
            continue;
        }
        // A heal over time already on the target is not cast again.
        if (!direct && target->HasAura(spellId, bot->GetGUID()))
            continue;
        if (!botAI->CanCastSpell(spellId, target))
            continue;
        spells.push_back({spellId, info->CalcCastTime(bot), info->SpellLevel, direct});
    }
    uint32 const spellId = CoaHealRequest::PickHeal(spells);
    why = spellId ? Reason::None : !any ? Reason::NoBot : !inRange ? Reason::Range : cd ? Reason::Cd : Reason::Busy;
    return spellId;
}

// The single target taunts of the bot: the ids, ready or not.
struct TauntState
{
    uint32 ready = 0;       // a ready one (lowest id)
    uint32 any = 0;
    uint32 cdLeftMs = 0;    // the shortest cooldown left when none is ready
    bool allImmune = false;
};

TauntState TauntsOf(Player* bot, Unit* target)
{
    TauntState state;
    bool immuneOnly = true;
    for (auto const& [spellId, known] : bot->GetSpellMap())
    {
        if (!known || known->State == PLAYERSPELL_REMOVED || !known->Active || LowerRank(bot, spellId))
            continue;
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
        if (!info || info->IsPassive() || info->IsAffectingArea() || !info->NeedsExplicitUnitTarget() ||
            !CoaIsTauntSpell(info))
            continue;
        if (!state.any || spellId < state.any)
            state.any = spellId;
        if (target && target->IsImmunedToSpell(info))
            continue;
        immuneOnly = false;
        if (bot->HasSpellCooldown(spellId))
        {
            uint32 const left = bot->GetSpellCooldownDelay(spellId);
            if (!state.cdLeftMs || left < state.cdLeftMs)
                state.cdLeftMs = left;
            continue;
        }
        if (!state.ready || spellId < state.ready)
            state.ready = spellId;
    }
    state.allImmune = state.any && immuneOnly;
    return state;
}

std::string TargetName(Unit* target) { return target ? target->GetName() : std::string("it"); }

std::string DoneText(Kind kind, Player* player, Unit* target, SpellInfo const* info)
{
    switch (kind)
    {
        case Kind::Heal:
            return target == player ? std::string("Healing you.") : "Healing " + TargetName(target) + ".";
        case Kind::Cc:
            return "Crowd control on " + TargetName(target) + (info ? " (" + std::string(info->SpellName[0]) + ")" : "") +
                   ", moon on it: don't break it.";
        default:
            return "Taunting " + TargetName(target) + "!";
    }
}

std::string NoneText(Kind kind, Reason reason)
{
    char const* what = kind == Kind::Heal ? "heal" : kind == Kind::Cc ? "crowd control" : "taunt";
    switch (reason)
    {
        case Reason::Off:
            return "That is turned off on this server.";
        case Reason::NotGroup:
            return "Group with us first.";
        case Reason::NoTarget:
            return std::string("Target it first, then ask for a ") + what + ".";
        case Reason::Full:
            return "Already at full health.";
        case Reason::Held:
            return "I already have it.";
        case Reason::Immune:
            return kind == Kind::Cc ? "That one can't be crowd controlled." : "That one can't be taunted.";
        case Reason::Cd:
            return kind == Kind::Taunt ? "No taunt ready, taking it back with threat."
                                       : std::string("Every ") + what + " we have is on cooldown.";
        case Reason::Range:
            return kind == Kind::Taunt ? "Out of range, coming for it." : std::string("Out of range for a ") + what + ".";
        case Reason::Busy:
            return std::string("Can't ") + what + " right now.";
        default:
            return kind == Kind::Heal ? "Nobody here can heal right now."
                   : kind == Kind::Cc ? "Nobody here has a crowd control."
                                      : "No tank here to taunt.";
    }
}

// The answer, where the ask was heard: the addon's line, or a sentence in the same chat.
void Answer(Player* bot, PlayerbotAI* botAI, Origin origin, Player* player, std::string const& line,
            std::string const& text)
{
    switch (origin)
    {
        case Origin::Addon:
            CoaAltAddon::Send(bot, player, line);
            break;
        case Origin::Whisper:
            botAI->Whisper(text, player->GetName());
            break;
        case Origin::Raid:
            if (bot->GetGroup() && bot->GetGroup()->isRaidGroup())
                botAI->SayToRaid(text);
            else
                botAI->SayToParty(text);
            break;
        case Origin::Party:
            botAI->SayToParty(text);
            break;
        case Origin::Say:
            botAI->Say(text);
            break;
    }
}

// What a bot did with an ask.
struct Result
{
    bool ok = false;
    bool final = false;  // answered for good (cast, held, immune, no target, a taunt by threat)
    bool full = false;   // heal: the target is at full health
    Reason reason = Reason::None;
    uint32 spellId = 0;
    Unit* target = nullptr;
};

bool BotFree(Player* bot)
{
    return bot->IsAlive() && !bot->IsMounted() && !bot->IsInFlight() && !bot->GetVehicle() &&
           !bot->HasUnitState(UNIT_STATE_CONTROLLED);
}

Result DoHeal(Player* bot, PlayerbotAI* botAI, Ask const& ask, Player* player, uint64 nowMs)
{
    Result r;
    Unit* target = UnitOf(bot, ask.target);
    if (!target || target == bot || !target->IsAlive() || !target->IsPlayer() || !player->IsFriendlyTo(target))
        target = player;
    r.target = target;
    if (!target->IsAlive())
    {
        r.final = r.full = true;
        return r;
    }
    if (target->GetHealth() >= target->GetMaxHealth())
    {
        r.full = true;
        r.reason = Reason::Full;
        return r;
    }
    if (!MayHeal(PlayerbotAI::IsHeal(bot), ask.anyCast, ask.heardMs, nowMs) || !BotFree(bot))
        return r;
    if (bot->IsNonMeleeSpellCast(false) || bot->isMoving())
    {
        r.reason = Reason::Busy;
        return r;
    }
    if (!bot->IsInMap(target))
    {
        r.reason = Reason::Range;
        return r;
    }
    Reason why = Reason::None;
    uint32 const spellId = HealFor(bot, botAI, target, why);
    r.reason = why;
    if (spellId && botAI->CastSpell(spellId, target))
    {
        r.ok = true;
        r.spellId = spellId;
    }
    else if (spellId)
        r.reason = Reason::Busy;
    return r;
}

Result DoCc(Player* bot, PlayerbotAI* botAI, Ask const& ask)
{
    Result r;
    Unit* target = UnitOf(bot, ask.target);
    r.target = target;
    if (!target || !target->IsAlive() || !bot->IsValidAttackTarget(target))
    {
        r.final = true;
        r.reason = Reason::NoTarget;
        return r;
    }
    if (BossLike(target) || UnderCc(target))
    {
        r.final = true;
        r.reason = Reason::Immune;
        return r;
    }
    if (!BotFree(bot))
        return r;
    std::vector<CcSpell> castable;
    bool any = false, valid = false, ready = false, inRange = false;
    bool const sight = bot->IsWithinLOSInMap(target);
    for (auto const& [spellId, known] : bot->GetSpellMap())
    {
        if (!known || known->State == PLAYERSPELL_REMOVED || !known->Active || LowerRank(bot, spellId))
            continue;
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
        if (!info || !IsCcSpell(info))
            continue;
        any = true;
        if (!info->CheckTargetCreatureType(target) || target->IsImmunedToSpell(info))
            continue;
        valid = true;
        if (bot->HasSpellCooldown(spellId))
            continue;
        ready = true;
        if (!sight || !bot->IsWithinDistInMap(target, info->GetMaxRange(false, bot)))
            continue;
        inRange = true;
        if (!botAI->CanCastSpell(spellId, target))
            continue;
        castable.push_back({spellId, info->GetMaxDuration(), CdTotal(info)});
    }
    r.reason = !any ? Reason::NoBot : !valid ? Reason::Immune : !ready ? Reason::Cd : !inRange ? Reason::Range
                                                                                              : Reason::Busy;
    if (castable.empty())
        return r;
    if (bot->IsNonMeleeSpellCast(false))
        bot->InterruptNonMeleeSpells(false);
    CcSpell const best = *std::min_element(castable.begin(), castable.end(), BetterCc);
    if (!botAI->CastSpell(best.id, target))
        return r;
    r.ok = r.final = true;
    r.reason = Reason::None;
    r.spellId = best.id;
    // The moon marks it (the bots' target choice leaves it alone), and the bots on it leave it.
    if (Group* group = bot->GetGroup())
    {
        group->SetTargetIcon(MOON, bot->GetGUID(), target->GetGUID());
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            PlayerbotAI* memberAI = member ? GET_PLAYERBOT_AI(member) : nullptr;
            if (!memberAI || !member->IsInWorld() || !member->IsInMap(bot))
                continue;
            if (member->GetVictim() == target)
                member->AttackStop();
            if (member->GetTarget() == target->GetGUID())
                member->SetTarget(ObjectGuid::Empty);
            if (memberAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get() == target)
                memberAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Set(nullptr);
        }
    }
    return r;
}

// The tank keeps the target: pinned like a Main Tank bar target (taunted again when it turns away).
void TakeTarget(Player* bot, PlayerbotAI* botAI, Unit* target)
{
    CoaPlayerMainTank::Unpin(bot);
    CoaPlayerMainTank::Pin(bot, target->GetGUID(), NO_ICON);
    botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Set(target);
    bot->SetTarget(target->GetGUID());
}

Result DoTaunt(Player* bot, PlayerbotAI* botAI, Ask const& ask)
{
    Result r;
    if (!PlayerbotAI::IsTank(bot) || !BotFree(bot))
        return r;
    Unit* target = UnitOf(bot, ask.target);
    r.target = target;
    if (!target || !target->IsAlive() || !bot->IsValidAttackTarget(target))
    {
        r.final = true;
        r.reason = Reason::NoTarget;
        return r;
    }
    if (target->GetVictim() == bot)
    {
        r.final = true;
        r.reason = Reason::Held;
        return r;
    }
    TauntState const taunts = TauntsOf(bot, target);
    if (!taunts.any)
    {
        r.reason = Reason::NoBot;
        return r;
    }
    if (taunts.allImmune)
    {
        r.final = true;
        r.reason = Reason::Immune;
        return r;
    }
    r.final = true;
    if (!taunts.ready)
    {
        // No taunt ready: the tank takes it with its threat, and says so.
        TakeTarget(bot, botAI, target);
        r.reason = Reason::Cd;
        return r;
    }
    SpellInfo const* info = sSpellMgr->GetSpellInfo(taunts.ready);
    if (!bot->IsWithinLOSInMap(target) || !bot->IsWithinDistInMap(target, info->GetMaxRange(false, bot)))
    {
        TakeTarget(bot, botAI, target);  // it walks to it and taunts on the way
        r.reason = Reason::Range;
        return r;
    }
    if (bot->IsNonMeleeSpellCast(false))
        bot->InterruptNonMeleeSpells(false);
    if (!botAI->CastSpell(taunts.ready, target))
    {
        r.final = false;
        r.reason = Reason::Busy;
        return r;
    }
    TakeTarget(bot, botAI, target);
    r.ok = true;
    r.reason = Reason::None;
    r.spellId = taunts.ready;
    return r;
}

uint32 CdLeftOf(Player* bot, uint32 spellId)
{
    return spellId && bot->HasSpellCooldown(spellId) ? bot->GetSpellCooldownDelay(spellId) : 0;
}

void SendState(Player* bot, Player* master)
{
    uint32 ready[3] = {0, 0, 0};
    uint32 next[3] = {0, 0, 0};
    auto soonest = [](uint32& slot, uint32 ms)
    {
        if (ms && (!slot || ms < slot))
            slot = ms;
    };
    if (Group* group = master->GetGroup())
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || !member->IsInWorld() || !member->IsAlive() || !member->IsInMap(bot) ||
                !GET_PLAYERBOT_AI(member) || IsRealPlayer(member))
                continue;
            if (PlayerbotAI::IsHeal(member))
                ++ready[0];
            if (uint32 const cc = BestCc(member, nullptr, false))
            {
                if (BestCc(member, nullptr, true))
                    ++ready[1];
                else
                    soonest(next[1], CdLeftOf(member, cc));
            }
            if (PlayerbotAI::IsTank(member))
            {
                TauntState const taunts = TauntsOf(member, nullptr);
                if (taunts.ready)
                    ++ready[2];
                else
                    soonest(next[2], taunts.cdLeftMs);
            }
        }
    CoaAltAddon::Send(bot, master, StateLine(Kind::Heal, ready[0], next[0]));
    CoaAltAddon::Send(bot, master, StateLine(Kind::Cc, ready[1], ready[1] ? 0 : next[1]));
    CoaAltAddon::Send(bot, master, StateLine(Kind::Taunt, ready[2], ready[2] ? 0 : next[2]));
    CoaAltAddon::Send(bot, master, "QA\t-");
}
}  // namespace

bool HearChat(Player* player, std::string const& msg, Origin origin, Player* receiver)
{
    if (!player)
        return false;
    Kind const kind = ParseLine(msg);
    if (kind == Kind::None || !KindOn(kind) || !player->GetGroup())
        return false;
    // A whisper is for the group of the bot whispered to: the player's group.
    if (receiver && !player->GetGroup()->IsMember(receiver->GetGUID()))
        return false;
    Hear(player, kind, player->GetTarget(), origin);
    return true;
}

bool Command(PlayerbotAI* botAI, std::string const& command, std::string const& arg)
{
    if (command != "qa" && command != "qastate")
        return false;
    Player* bot = botAI ? botAI->GetBot() : nullptr;
    Player* master = botAI ? botAI->GetMaster() : nullptr;
    if (!bot || !master || !master->IsInWorld())
        return true;
    if (command == "qastate")
    {
        SendState(bot, master);
        return true;
    }
    Kind kind = Kind::None;
    uint64_t guid = 0;
    if (!ParseAddon(arg, kind, guid))
    {
        CoaAltAddon::Send(bot, master, "QA\terr\tunknown");
        return true;
    }
    if (!KindOn(kind))
    {
        CoaAltAddon::Send(bot, master, NoneLine(kind, Reason::Off));
        return true;
    }
    if (!master->GetGroup() || !master->GetGroup()->IsMember(bot->GetGUID()))
    {
        CoaAltAddon::Send(bot, master, NoneLine(kind, Reason::NotGroup));
        return true;
    }
    if (!Hear(master, kind, guid ? ObjectGuid(guid) : master->GetTarget(), Origin::Addon))
        CoaAltAddon::Send(bot, master, NoneLine(kind, Reason::Busy));  // the same ask again, a moment ago
    return true;
}

void Tick(Player* bot, PlayerbotAI* botAI)
{
    if (!anyAsk || !bot || !botAI || !bot->IsInWorld())
        return;
    Group* group = bot->GetGroup();
    if (!group)
        return;
    uint64 const nowMs = NowMs();
    Ask ask;
    bool answerNone = false, act = false;
    {
        std::lock_guard<std::mutex> guard(askLock);
        asks.erase(std::remove_if(asks.begin(), asks.end(),
                                  [nowMs](Ask const& a)
                                  {
                                      bool const open = Open(a.kind, a.heardMs, nowMs);
                                      // Answered and over, or never answered for long (no bot of its group left).
                                      return (!open && (a.answered || a.done)) || nowMs > a.heardMs + HEAL_WINDOW_MS * 2;
                                  }),
                   asks.end());
        if (asks.empty())
        {
            anyAsk = false;
            return;
        }
        for (Ask& a : asks)
        {
            if (!group->IsMember(a.player))
                continue;
            bool const open = Open(a.kind, a.heardMs, nowMs);
            bool const silent = a.kind == Kind::Heal ? !a.anyCast && HealSilent(a.heardMs, nowMs) : !open;
            if (!a.answered && !a.done && silent && !a.claimed)
            {
                a.answered = true;
                if (a.kind != Kind::Heal)
                    a.done = true;
                ask = a;
                answerNone = true;
                break;
            }
            if (!open || a.done || a.claimed)
                continue;
            if (a.kind != Kind::Heal)
                a.claimed = true;
            ask = a;
            act = true;
            break;
        }
    }
    if (!answerNone && !act)
        return;
    Player* player = ObjectAccessor::FindConnectedPlayer(ask.player);
    if (answerNone)
    {
        if (player && player->IsInWorld())
            Answer(bot, botAI, ask.origin, player, NoneLine(ask.kind, ask.reason), NoneText(ask.kind, ask.reason));
        return;
    }
    Result r;
    if (player && player->IsInWorld() && !bot->IsInMap(player))
    {
        // A bot of the group elsewhere (another map) leaves the ask to the others.
        std::lock_guard<std::mutex> guard(askLock);
        for (Ask& a : asks)
            if (a.id == ask.id)
                a.claimed = false;
        return;
    }
    if (player && player->IsInWorld())
    {
        if (ask.kind == Kind::Heal)
            r = DoHeal(bot, botAI, ask, player, nowMs);
        else if (ask.kind == Kind::Cc)
            r = DoCc(bot, botAI, ask);
        else
            r = DoTaunt(bot, botAI, ask);
    }
    else
        r.final = true;  // the player is gone
    bool sendDone = false, sendNone = false;
    Reason noneReason = Reason::None;
    {
        std::lock_guard<std::mutex> guard(askLock);
        auto it = std::find_if(asks.begin(), asks.end(), [&](Ask const& a) { return a.id == ask.id; });
        if (it == asks.end())
            return;
        Ask& a = *it;
        a.claimed = false;
        if (r.reason != Reason::None && !r.ok)
            a.reason = Stronger(a.reason, r.reason);
        if (a.kind == Kind::Heal)
        {
            if (r.ok)
                a.anyCast = true;
            if (r.full || r.final)
            {
                // Full at once: "full"; full after the heals: done, nothing more to say.
                if (!a.answered && !a.anyCast && r.reason == Reason::Full)
                {
                    a.answered = true;
                    sendNone = true;
                    noneReason = Reason::Full;
                }
                a.done = true;
            }
            if (r.ok && !a.answered)
                a.answered = sendDone = true;
        }
        else if (r.ok || r.final)
        {
            a.done = true;
            if (!a.answered)
            {
                a.answered = true;
                sendDone = r.ok;
                sendNone = !r.ok;
                noneReason = r.reason;
            }
        }
    }
    if (!player || !player->IsInWorld())
        return;
    if (sendDone)
    {
        SpellInfo const* info = sSpellMgr->GetSpellInfo(r.spellId);
        uint32 const cdLeft = ask.kind == Kind::Heal ? 0 : CdLeftOf(bot, r.spellId);
        Answer(bot, botAI, ask.origin, player, DoneLine(ask.kind, bot->GetName(), r.spellId, cdLeft),
               DoneText(ask.kind, player, r.target, info));
    }
    else if (sendNone)
        Answer(bot, botAI, ask.origin, player, NoneLine(ask.kind, noneReason), NoneText(ask.kind, noneReason));
}

uint32_t BestCc(Player* bot, Unit* target, bool readyOnly)
{
    if (!bot)
        return 0;
    std::vector<CcSpell> spells;
    for (auto const& [spellId, known] : bot->GetSpellMap())
    {
        if (!known || known->State == PLAYERSPELL_REMOVED || !known->Active || LowerRank(bot, spellId))
            continue;
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
        if (!info || !IsCcSpell(info))
            continue;
        if (target && (!info->CheckTargetCreatureType(target) || target->IsImmunedToSpell(info)))
            continue;
        if (readyOnly && bot->HasSpellCooldown(spellId))
            continue;
        spells.push_back({spellId, info->GetMaxDuration(), CdTotal(info)});
    }
    if (spells.empty())
        return 0;
    return std::min_element(spells.begin(), spells.end(), BetterCc)->id;
}

char const* SpellCategory(SpellInfo const* info, uint32_t botCc)
{
    if (!info)
        return "-";
    uint32_t const triggers[3] = {info->Effects[0].TriggerSpell, info->Effects[1].TriggerSpell,
                                  info->Effects[2].TriggerSpell};
    bool const burst = CoaBossLustRule::IsGroupBurst(info->ExcludeTargetAuraSpell, triggers);
    return Category(burst, IsDefensive(info), CdTotal(info), botCc && info->Id == botCc);
}
}  // namespace CoaQuickAsk
