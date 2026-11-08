/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "BWLHelpers.h"
#include "AiObjectContext.h"
#include "Group.h"
#include "PlayerbotAIConfig.h"
#include "SpellAuras.h"

#include <cstddef>

namespace BlackwingLairHelpers
{
    bool IsActiveSuppressionDeviceInRange(GameObject const* go, Player const* bot)
    {
        constexpr float suppressionDeviceInteractionDistance = 15.0f;
        return go &&
               go->GetEntry() == static_cast<uint32>(BlackwingLairGameObjects::GO_SUPPRESSION_DEVICE) &&
               go->GetDistance(bot) < suppressionDeviceInteractionDistance &&
               go->GetGoState() == GO_STATE_READY;
    }

    bool AreRazorgoreEggsAlive(PlayerbotAI* botAI)
    {
        GuidVector gos = botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest game objects")->Get();
        for (auto const& guid : gos)
        {
            GameObject const* go = botAI->GetGameObject(guid);
            if (go && go->GetEntry() == static_cast<uint32>(BlackwingLairGameObjects::GO_BLACK_DRAGON_EGG))
                return true;
        }
        return false;
    }

    bool IsRazorgoreOffTank(Player* bot)
    {
        return PlayerbotAI::IsAssistTankOfIndex(bot, 0, true);
    }

    bool IsNonBABotNearPosition(Player const* bot, Position const& position, float distance)
    {
        Group const* group = bot->GetGroup();
        if (!group)
            return false;

        for (GroupReference const* gref = group->GetFirstMember(); gref; gref = gref->next())
        {
            Player const* p = gref->GetSource();
            if (!p || p == bot || !p->IsAlive() || p->GetMapId() != bot->GetMapId())
                continue;

            if (HasBurningAdrenaline(p))
                continue;

            if (p->GetDistance2d(position.GetPositionX(), position.GetPositionY()) < distance)
                return true;
        }

        return false;
    }

    namespace
    {
        template <std::size_t N>
        bool HasAnyOf(Unit const* unit, BlackwingLairSpells const (&spells)[N])
        {
            if (!unit)
                return false;

            for (BlackwingLairSpells spell : spells)
                if (unit->HasAura(static_cast<uint32>(spell)))
                    return true;

            return false;
        }

        constexpr BlackwingLairSpells BURNING_ADRENALINE_AURAS[] =
        {
            BlackwingLairSpells::SPELL_BURNING_ADRENALINE,
            BlackwingLairSpells::SPELL_BURNING_ADRENALINE_COA,
            BlackwingLairSpells::SPELL_BURNING_ADRENALINE_COA_STACK_D0,
            BlackwingLairSpells::SPELL_BURNING_ADRENALINE_COA_STACK_D1,
            BlackwingLairSpells::SPELL_BURNING_ADRENALINE_COA_STACK_D2,
            BlackwingLairSpells::SPELL_BURNING_ADRENALINE_COA_STACK_D3
        };

        constexpr BlackwingLairSpells SHADOW_FLAME_CLOAK_AURAS[] =
        {
            BlackwingLairSpells::SPELL_ONYXIA_SCALE_CLOAK,
            BlackwingLairSpells::SPELL_BLACK_DRAGON_SCALE_CLOAK_COA
        };
    }

    bool HasBurningAdrenaline(Unit const* unit)
    {
        return HasAnyOf(unit, BURNING_ADRENALINE_AURAS);
    }

    bool HasShadowFlameCloak(Unit const* unit)
    {
        return HasAnyOf(unit, SHADOW_FLAME_CLOAK_AURAS);
    }

    bool HasCurableBroodAfflictionBronze(Unit const* unit)
    {
        if (!unit)
            return false;

        if (unit->HasAura(static_cast<uint32>(BlackwingLairSpells::SPELL_BROOD_AFFLICTION_BRONZE)))
            return true;

        // CoA: Hourglass Sand does nothing while the bronze blessing is up
        // (spell_chromaggus_coa_hourglass_sand), so do not waste casts on it.
        return unit->HasAura(static_cast<uint32>(BlackwingLairSpells::SPELL_BROOD_AFFLICTION_BRONZE_COA)) &&
               !unit->HasAura(static_cast<uint32>(BlackwingLairSpells::SPELL_DRAGONFLIGHT_BLESSING_BRONZE_COA));
    }

    uint32 GetHourglassSandSpell(Unit const* unit)
    {
        // The CoA affliction is only removed by the CoA Hourglass Sand (2111023, cast by
        // item 19183 on CoA), the stock affliction by the stock spell.
        if (unit && unit->HasAura(static_cast<uint32>(BlackwingLairSpells::SPELL_BROOD_AFFLICTION_BRONZE_COA)) &&
            !unit->HasAura(static_cast<uint32>(BlackwingLairSpells::SPELL_DRAGONFLIGHT_BLESSING_BRONZE_COA)))
            return static_cast<uint32>(BlackwingLairSpells::SPELL_HOURGLASS_SAND_COA);

        return static_cast<uint32>(BlackwingLairSpells::SPELL_HOURGLASS_SAND);
    }

    bool IsCorruptedWhelp(Unit const* unit)
    {
        if (!unit)
            return false;

        const uint32 entry = unit->GetEntry();
        return entry == static_cast<uint32>(BlackwingLairNPCs::NPC_CORRUPTED_RED_WHELP) ||
            entry == static_cast<uint32>(BlackwingLairNPCs::NPC_CORRUPTED_GREEN_WHELP) ||
            entry == static_cast<uint32>(BlackwingLairNPCs::NPC_CORRUPTED_BLUE_WHELP) ||
            entry == static_cast<uint32>(BlackwingLairNPCs::NPC_CORRUPTED_BRONZE_WHELP);
    }

    bool IsNefarianDoNotAttackTarget(Unit const* unit)
    {
        if (!unit)
            return false;

        if (unit->GetEntry() == static_cast<uint32>(BlackwingLairNPCs::NPC_SHADOW_CLONE_COA) &&
            unit->GetTypeId() == TYPEID_UNIT)
            return true;

        if (unit->GetTypeId() != TYPEID_PLAYER || !unit->IsCharmed())
            return false;

        Unit const* charmer = unit->GetCharmer();
        return charmer && charmer->GetEntry() == static_cast<uint32>(BlackwingLairNPCs::NPC_LORD_VICTOR_NEFARIUS);
    }
}

// ---- CoA Combat (lot 7) ----

namespace
{
    constexpr uint32 SCORCHED_AURAS[] = {2110813, 2110814, 2110815, 2110816};
    constexpr uint32 SHADOW_OF_EBONROC_AURAS[] = {2110837, 2110838, 2110839, 2110840};
    constexpr uint32 BROOD_AFFLICTION_RED = 2111012;
    constexpr uint32 BLESSING_RED = 2111007;

    uint32 ScorchedStacks(Unit const* unit)
    {
        uint32 stacks = 0;
        for (uint32 spellId : SCORCHED_AURAS)
            if (Aura const* aura = unit->GetAura(spellId))
                stacks += aura->GetStackAmount();
        return stacks;
    }

    bool HasShadowOfEbonroc(Unit const* unit)
    {
        for (uint32 spellId : SHADOW_OF_EBONROC_AURAS)
            if (unit->HasAura(spellId))
                return true;
        return false;
    }
}

bool BlackwingLairHelpers::BwlCoaFixesOn()
{
    return sPlayerbotAIConfig.coaCombatBlackwingLairEnable;
}

bool BlackwingLairHelpers::IsDrakeTankMarked(Unit const* tank, bool firemaw)
{
    return firemaw ? ScorchedStacks(tank) >= SCORCHED_SWAP_STACKS : HasShadowOfEbonroc(tank);
}

bool BlackwingLairHelpers::CanTakeDrake(Unit const* tank, bool firemaw)
{
    return firemaw ? ScorchedStacks(tank) < SCORCHED_SWAP_STACKS / 2 : !HasShadowOfEbonroc(tank);
}

bool BlackwingLairHelpers::HasCurableRedAffliction(Unit const* unit)
{
    return unit->HasAura(BROOD_AFFLICTION_RED) && !unit->HasAura(BLESSING_RED);
}

bool BlackwingLairHelpers::IsDominionCloneNear(Unit const* unit, float range)
{
    std::list<Creature*> clones;
    unit->GetCreatureListWithEntryInGrid(clones, static_cast<uint32>(BlackwingLairNPCs::NPC_SHADOW_CLONE_COA), range);
    for (Creature* clone : clones)
        if (clone && clone->IsAlive())
            return true;
    return false;
}
