/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaGraveCurse.h"

#include "AttackAction.h"
#include "CoaGraveCurseRule.h"
#include "Creature.h"
#include "Group.h"
#include "Playerbots.h"
#include "PlayerbotAIConfig.h"
#include "ReachTargetActions.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <list>

namespace
{
    bool On(Player* bot) { return bot && bot->IsAlive() && sPlayerbotAIConfig.coaCombatCallOfTheGraveEnable; }

    bool CarriesGraveCurse(Unit* unit)
    {
        for (unsigned spellId : CoaGraveCurseRule::CURSES)
            if (unit->HasAura(spellId))
                return true;
        return false;
    }
}

uint32 CoaGraveCurse::CurseRemovalSpell(Player* bot)
{
    for (auto const& [spellId, spell] : bot->GetSpellMap())
    {
        if (!spell || spell->State == PLAYERSPELL_REMOVED || !spell->Active)
            continue;
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
        if (!info || info->IsPassive() || !info->IsPositive())
            continue;
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            if (info->Effects[i].Effect == SPELL_EFFECT_DISPEL && info->Effects[i].MiscValue == DISPEL_CURSE)
                return spellId;
    }
    return 0;
}

Player* CoaGraveCurse::CursedMember(Player* bot)
{
    Group* group = bot->GetGroup();
    if (!group)
        return CarriesGraveCurse(bot) ? bot : nullptr;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (member && member->IsAlive() && member->IsInWorld() && member->GetMapId() == bot->GetMapId() &&
            bot->GetDistance(member) < 40.0f && CarriesGraveCurse(member))
            return member;
    }
    return nullptr;
}

Creature* CoaGraveCurse::GraveCasterNear(Unit* from, float range)
{
    for (unsigned base : CoaGraveCurseRule::CASTERS)
        for (unsigned offset : {0u, 100000u, 200000u, 300000u})
        {
            std::list<Creature*> casters;
            from->GetCreatureListWithEntryInGrid(casters, base + offset, range);
            for (Creature* caster : casters)
                if (caster && caster->IsAlive())
                    return caster;
        }
    return nullptr;
}

bool CoaGraveCurseDispelTrigger::IsActive()
{
    return On(bot) && bot->IsInCombat() && CoaGraveCurse::CurseRemovalSpell(bot) && CoaGraveCurse::CursedMember(bot);
}

bool CoaGraveCurseDispelAction::Execute(Event /*event*/)
{
    uint32 const spellId = CoaGraveCurse::CurseRemovalSpell(bot);
    Player* member = CoaGraveCurse::CursedMember(bot);
    if (!spellId || !member || !botAI->CanCastSpell(spellId, member))
        return false;
    return botAI->CastSpell(spellId, member);
}

bool CoaGraveCurseLeaveTrigger::IsActive()
{
    if (!On(bot))
        return false;
    Creature* caster = CoaGraveCurse::GraveCasterNear(bot, CoaGraveCurseRule::KEEP_YARDS);
    return caster && CoaGraveCurseRule::MustLeave(PlayerbotAI::IsTank(bot), PlayerbotAI::IsMelee(bot),
                                                  bot->GetDistance2d(caster));
}

bool CoaGraveCurseLeaveAction::Execute(Event /*event*/)
{
    Creature* caster = CoaGraveCurse::GraveCasterNear(bot, CoaGraveCurseRule::KEEP_YARDS);
    if (!caster)
        return false;
    return MoveAway(caster, CoaGraveCurseRule::KEEP_YARDS + 2.0f - bot->GetDistance2d(caster));
}

float CoaGraveCurseMeleeMultiplier::GetValue(Action* action)
{
    if (!On(bot) || PlayerbotAI::IsTank(bot) || !PlayerbotAI::IsMelee(bot))
        return 1.0f;
    if (!dynamic_cast<ReachMeleeAction*>(action) && !dynamic_cast<MeleeAction*>(action))
        return 1.0f;
    // Striking a caster, or a mob a caster stands beside, means standing in the curse's reach.
    Unit* target = AI_VALUE(Unit*, "current target");
    if (!target)
        return 1.0f;
    return CoaGraveCurse::GraveCasterNear(target, CoaGraveCurseRule::KEEP_YARDS) ? 0.0f : 1.0f;
}

void CoaGraveCurseStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Removed long before the 60 s are up, but ahead of the rotation: the burst is a death sentence in mythic.
    triggers.push_back(
        new TriggerNode("coa combat grave curse dispel", {NextAction("coa combat grave curse dispel", ACTION_HIGH + 6)}));
    triggers.push_back(
        new TriggerNode("coa combat grave curse leave", {NextAction("coa combat grave curse leave", ACTION_HIGH + 7)}));
}

void CoaGraveCurseStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new CoaGraveCurseMeleeMultiplier(botAI));
}
