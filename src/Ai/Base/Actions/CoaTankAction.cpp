/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaTankAction.h"

#include "CoaAltAddon.h"
#include "CoaPlayerMainTank.h"
#include "CoaSpecialization.h"
#include "Event.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Playerbots.h"
#include "PositionValue.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

namespace
{
constexpr uint8 NoIcon = 0xFF;

std::string Role(Player* bot)
{
    if (PlayerbotAI::IsTank(bot))
        return "tank";
    return PlayerbotAI::IsHeal(bot) ? "heal" : "dps";
}

bool CanTank(Player* bot)
{
    return PlayerbotAI::IsTank(bot) || CoaCanSwitchRole(bot, CoaRole::Tank);
}

bool Busy(Player* bot)
{
    return !bot->IsAlive() || bot->GetVehicle() || bot->HasUnitState(UNIT_STATE_CONTROLLED);
}

// Whether a spell forces its target to attack the caster: SPELL_EFFECT_ATTACK_ME (Taunt, Growl, Hand of Reckoning,
// Dark Command) or a SPELL_AURA_MOD_TAUNT aura (most taunts of the CoA classes in the client DBC), itself or through
// the spell it triggers, as the CoA rotation reads them.
bool IsTaunt(SpellInfo const* info, uint8 depth = 0)
{
    for (SpellEffectInfo const& effect : info->Effects)
    {
        if (effect.Effect == SPELL_EFFECT_ATTACK_ME ||
            (effect.Effect == SPELL_EFFECT_APPLY_AURA && effect.ApplyAuraName == SPELL_AURA_MOD_TAUNT))
            return true;
        if (depth < 2 && effect.TriggerSpell && effect.TriggerSpell != info->Id)
            if (SpellInfo const* triggered = sSpellMgr->GetSpellInfo(effect.TriggerSpell))
                if (IsTaunt(triggered, depth + 1))
                    return true;
    }
    return false;
}

// The single target taunt the bot casts on `target` for the Main Tank bar: never one that hits an area (Challenging
// Shout), nor one the target is immune to; a ready one before one on cooldown, then the lowest id, so that the choice
// does not hang on the order of the spell map (06/11 review, point 6). 0 when the bot has none.
uint32 TauntSpell(Player* bot, Unit* target)
{
    uint32 best = 0;
    bool bestReady = false;
    for (auto const& [spellId, spell] : bot->GetSpellMap())
    {
        if (spell->State == PLAYERSPELL_REMOVED || !spell->Active)
            continue;
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
        if (!info || info->IsPassive() || info->IsAffectingArea() || !info->NeedsExplicitUnitTarget() || !IsTaunt(info))
            continue;
        if (target && target->IsImmunedToSpell(info))
            continue;
        bool const ready = !bot->HasSpellCooldown(spellId);
        if (!best || (ready && !bestReady) || (ready == bestReady && spellId < best))
        {
            best = spellId;
            bestReady = ready;
        }
    }
    return best;
}
// ObjectAccessor::GetUnit looks on the map of `from`, and a player between two maps (a teleport, a loading screen) has
// none: nullptr then, not an assert (06/11 review, point 12).
Unit* UnitOnMapOf(Player* from, ObjectGuid guid)
{
    if (!from || !from->IsInWorld() || !from->FindMap())
        return nullptr;
    return ObjectAccessor::GetUnit(*from, guid);
}
}  // namespace

void CoaTankAction::Reply(std::string const& action, std::string const& result, uint8 icon, Unit* target)
{
    std::string const name = target ? target->GetName() : "-";
    uint32 const clientIcon = icon == NoIcon ? 0 : uint32(icon) + 1;
    if (CoaAltAddon::MasterHasAddon(botAI->GetMaster()))
    {
        CoaAltAddon::SendToMaster(botAI, "TK\t" + action + "\t" + result + "\t" + std::to_string(clientIcon) + "\t" + name);
        return;
    }
    botAI->TellMaster("Tank " + action + ": " + result + (target ? " (" + name + ")" : ""));
}

void CoaTankAction::SendStatus(Player* tank)
{
    uint8 icon = NoIcon;
    ObjectGuid const pinned = CoaPlayerMainTank::Pinned(tank, &icon);
    Unit* target = pinned ? UnitOnMapOf(tank, pinned) : nullptr;
    std::string const line = "TS\t" + tank->GetName() + "\t" + Role(tank) + "\t" + (CanTank(tank) ? "1" : "0") + "\t" +
                             std::to_string(icon == NoIcon ? 0 : uint32(icon) + 1) + "\t" +
                             (target ? target->GetName() : "-");
    if (CoaAltAddon::MasterHasAddon(botAI->GetMaster()))
        CoaAltAddon::SendToMaster(botAI, line);
    else
        botAI->TellMaster(tank->GetName() + ": " + Role(tank) + (target ? ", on " + target->GetName() : ", free"));
}

Unit* CoaTankAction::MasterTarget(Player* master)
{
    if (master->FindMap() != bot->FindMap())
        return nullptr;
    Unit* target = master->GetTarget() ? UnitOnMapOf(master, master->GetTarget()) : nullptr;
    if (!target || !target->IsAlive() || target->GetMap() != bot->GetMap() || !bot->IsValidAttackTarget(target))
        return nullptr;
    return target;
}

bool CoaTankAction::Taunt(Unit* target, std::string const& action)
{
    uint32 const spellId = TauntSpell(bot, target);
    SpellInfo const* info = spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
    if (!info)
    {
        Reply(action, "not_tank", NoIcon, target);
        return false;
    }
    if (bot->HasSpellCooldown(spellId))
    {
        Reply(action, "cooldown", NoIcon, target);
        return false;
    }
    if (!bot->IsWithinDistInMap(target, info->GetMaxRange(false, bot)))
    {
        Reply(action, "out_of_range", NoIcon, target);
        return false;
    }
    if (!botAI->CastSpell(spellId, target))
    {
        Reply(action, "busy", NoIcon, target);
        return false;
    }
    return true;
}

bool CoaTankAction::PinTarget(Unit* target, std::string const& action)
{
    CoaPlayerMainTank::Unpin(bot);
    uint8 icon = NoIcon;
    if (Group* group = bot->GetGroup())
    {
        icon = CoaPlayerMainTank::NextFreeIcon(group);
        if (icon != NoIcon)
            group->SetTargetIcon(icon, bot->GetGUID(), target->GetGUID());
    }
    CoaPlayerMainTank::Pin(bot, target->GetGUID(), icon);
    Reply(action, "ok", icon, target);
    return true;
}

void CoaTankAction::ReleaseHold()
{
    botAI->ChangeStrategy("+follow,-stay", BOT_STATE_NON_COMBAT);
    botAI->ChangeStrategy("-stay", BOT_STATE_COMBAT);
    ResetReturnPosition();
    ResetStayPosition();
}

bool CoaTankAction::Execute(Event event)
{
    Player* master = GetMaster();
    Player* requester = event.getOwner() ? event.getOwner() : master;
    if (!master || requester != master || GET_PLAYERBOT_AI(master))
        return false;

    std::string param = event.getParam();
    param.erase(0, param.find_first_not_of(' '));
    std::string command = param;
    std::string arg;
    if (size_t const space = param.find(' '); space != std::string::npos)
    {
        command = param.substr(0, space);
        arg = param.substr(space + 1);
        arg.erase(0, arg.find_first_not_of(' '));
    }

    if (!bot->IsInSameRaidWith(master))
    {
        Reply(command, "not_in_group");
        return false;
    }

    if (command == "main")
    {
        if (arg == "on" || arg == "off")
            CoaPlayerMainTank::Set(master, arg == "on");
        // The flag is the player's: answered ok, its state in the last field (contract with CoA Addon).
        if (CoaAltAddon::MasterHasAddon(master))
            CoaAltAddon::SendToMaster(botAI, std::string("TK\tmain\tok\t0\t") +
                                                 (CoaPlayerMainTank::IsSet(master) ? "on" : "off"));
        else
            botAI->TellMaster(std::string("Main tank: ") + (CoaPlayerMainTank::IsSet(master) ? "on" : "off"));
        return true;
    }

    if (command == "status")
    {
        if (arg != "all")
        {
            SendStatus(bot);
            return true;
        }
        if (Group* group = bot->GetGroup())
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                if (Player* member = ref->GetSource())
                    if (PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member))
                        if (memberAI->GetMaster() == master && CanTank(member))
                            SendStatus(member);
        return true;
    }

    if (command == "spec")
    {
        if (arg != "tank" && arg != "dps")
        {
            Reply("spec", "busy");
            return false;
        }
        if (Busy(bot) || bot->IsInCombat())
        {
            Reply("spec", "busy");
            return false;
        }
        CoaRole const role = arg == "tank" ? CoaRole::Tank : CoaRole::Dps;
        if (!CoaCanSwitchRole(bot, role))
        {
            Reply("spec", "not_tank");
            return false;
        }
        if (role != CoaRole::Tank)
        {
            CoaPlayerMainTank::Unpin(bot);
            ReleaseHold();
        }
        bool const switched = CoaSwitchRole(bot, role);
        if (CoaAltAddon::MasterHasAddon(botAI->GetMaster()))
            CoaAltAddon::SendToMaster(botAI, "TK\tspec\t" + std::string(switched ? "ok" : "busy") + "\t0\t" + Role(bot));
        else
            botAI->TellMaster("Spec: " + Role(bot));
        return switched;
    }

    if (command == "clear")
    {
        CoaPlayerMainTank::Unpin(bot);
        ReleaseHold();
        Reply("clear", "ok");
        return true;
    }

    if (!PlayerbotAI::IsTank(bot))
    {
        Reply(command, "not_tank");
        return false;
    }
    if (Busy(bot))
    {
        Reply(command, "busy");
        return false;
    }

    if (command == "hold")
    {
        SetReturnPosition(master->GetPositionX(), master->GetPositionY(), master->GetPositionZ());
        SetStayPosition(master->GetPositionX(), master->GetPositionY(), master->GetPositionZ());
        botAI->ChangeStrategy("+stay,-follow", BOT_STATE_NON_COMBAT);
        botAI->ChangeStrategy("+stay", BOT_STATE_COMBAT);
        bot->GetMotionMaster()->MovePoint(0, master->GetPositionX(), master->GetPositionY(), master->GetPositionZ());
        Reply("hold", "ok");
        return true;
    }

    Unit* target = MasterTarget(master);

    if (command == "swap" && arg == "back")
    {
        uint8 icon = NoIcon;
        ObjectGuid const pinned = CoaPlayerMainTank::Pinned(bot, &icon);
        Unit* held = pinned ? ObjectAccessor::GetUnit(*bot, pinned) : target;
        CoaPlayerMainTank::Unpin(bot);
        if (held)
            CoaPlayerMainTank::Release(bot, held->GetGUID());
        Reply("swap back", "ok", NoIcon, held);
        return true;
    }

    if (!target)
    {
        Reply(command, "no_target");
        return false;
    }

    if (command == "target")
        return PinTarget(target, "target");

    if (command == "taunt")
    {
        if (!Taunt(target, "taunt"))
            return false;
        Reply("taunt", "ok", NoIcon, target);
        return true;
    }

    if (command == "swap")
    {
        PinTarget(target, "swap");
        return Taunt(target, "swap");
    }

    botAI->TellMaster("Usage: tank main on|off|status, tank target, tank taunt, tank swap [back], tank hold, tank clear, "
                      "tank status [all], tank spec tank|dps");
    return false;
}
