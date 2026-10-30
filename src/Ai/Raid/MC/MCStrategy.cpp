/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "MCStrategy.h"
#include "MCHelpers.h"
#include "MCMultipliers.h"
#include "Playerbots.h"
#include "RtiTargetValue.h"
#include "Strategy.h"

using namespace MoltenCoreHelpers;

void RaidMcStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Lucifron
    triggers.push_back(
        new TriggerNode("mc lucifron shadow resistance",
                        { NextAction("mc lucifron shadow resistance", ACTION_RAID) }));

    // Magmadar
    // Panic fears everyone in 45 yd (stock 19408) or 100 yd (CoA 2105309): no range keeps a raid out of it, and a
    // stand-off at 35 yd only put the healers out of reach of the tank and the purgers out of reach of Frenzy
    // (purges reach 30 yd). A tank takes him back from a non-tank once the fear ends.
    triggers.push_back(
        new TriggerNode("mc magmadar fire resistance",
                        { NextAction("mc magmadar fire resistance", ACTION_RAID) }));
    triggers.push_back(
        new TriggerNode("mc magmadar backup taunt",
                        { NextAction("mc magmadar backup taunt", ACTION_RAID + 1) }));
    // CoA: his Core Hounds come back while another one fights, so the skull and the damage stay on him.
    triggers.push_back(
        new TriggerNode("mc magmadar mark",
                        { NextAction("mc magmadar mark", ACTION_RAID) }));
    triggers.push_back(
        new TriggerNode("mc magmadar lava bomb",
                        { NextAction("mc move from ground effect", ACTION_RAID) }));

    // Gehennas
    triggers.push_back(
        new TriggerNode("mc gehennas shadow resistance",
                        { NextAction("mc gehennas shadow resistance", ACTION_RAID) }));
    triggers.push_back(
        new TriggerNode("mc gehennas mark",
                        { NextAction("mc gehennas mark", ACTION_RAID) }));
    triggers.push_back(
        new TriggerNode("mc gehennas rain of fire",
                        { NextAction("mc move from ground effect", ACTION_RAID) }));
    triggers.push_back(
        new TriggerNode("mc gehennas flame orb cast",
                        { NextAction("mc gehennas clear flame orb spawn", ACTION_RAID + 2) }));
    triggers.push_back(
        new TriggerNode("mc gehennas flame orb",
                        { NextAction("mc gehennas move from flame orb", ACTION_RAID) }));

    // Garr
    triggers.push_back(
        new TriggerNode("mc garr fire resistance",
                        { NextAction("mc garr fire resistance", ACTION_RAID) }));

    // Baron Geddon
    triggers.push_back(
        new TriggerNode("mc baron geddon fire resistance",
                        { NextAction("mc baron geddon fire resistance", ACTION_RAID) }));
    triggers.push_back(
        new TriggerNode("mc living bomb debuff",
                        { NextAction("mc move from group", ACTION_RAID) }));
    triggers.push_back(
        new TriggerNode("mc baron geddon inferno",
                        { NextAction("mc move from baron geddon", ACTION_RAID) }));

    // Shazzrah
    triggers.push_back(
        new TriggerNode("mc shazzrah ranged",
                        { NextAction("mc shazzrah move away", ACTION_RAID) }));
    triggers.push_back(
        new TriggerNode("mc shazzrah backup taunt",
                        { NextAction("mc shazzrah backup taunt", ACTION_RAID + 1) }));

    // Sulfuron Harbinger
    // Alternatively, shadow resistance is also possible.
    triggers.push_back(
        new TriggerNode("mc sulfuron harbinger fire resistance",
                        { NextAction("mc sulfuron harbinger fire resistance", ACTION_RAID) }));

    // Golemagg the Incinerator
    triggers.push_back(
        new TriggerNode("mc golemagg fire resistance",
                        { NextAction("mc golemagg fire resistance", ACTION_RAID) }));
    triggers.push_back(
        new TriggerNode("mc golemagg mark boss",
                        { NextAction("mc golemagg mark boss", ACTION_RAID) }));
    triggers.push_back(
        new TriggerNode("mc golemagg is main tank",
                        { NextAction("mc golemagg main tank attack golemagg", ACTION_RAID) }));
    triggers.push_back(
        new TriggerNode("mc golemagg is assist tank",
                        { NextAction("mc golemagg assist tank attack core rager", ACTION_RAID) }));
    triggers.push_back(
        new TriggerNode("mc golemagg magma splash",
                        { NextAction("mc golemagg back off", ACTION_RAID + 1) }));
    triggers.push_back(
        new TriggerNode("mc golemagg is healer",
                        { NextAction("mc golemagg healer position", ACTION_RAID) }));

    // Majordomo Executus
    triggers.push_back(
        new TriggerNode("mc majordomo shadow resistance",
                        { NextAction("mc majordomo shadow resistance", ACTION_RAID) }));
    triggers.push_back(
        new TriggerNode("mc majordomo damage shield",
                        { NextAction("mc majordomo stop attack", ACTION_RAID + 1) }));

    // Ragnaros
    triggers.push_back(
        new TriggerNode("mc ragnaros fire resistance",
                        { NextAction("mc ragnaros fire resistance", ACTION_RAID) }));

    // Trash
    triggers.push_back(
        new TriggerNode("mc core hound mark",
                        { NextAction("mc core hound mark", ACTION_RAID) }));

    // Anywhere in MC: escaping a lava pool outranks everything else.
    triggers.push_back(
        new TriggerNode("mc in lava",
                        { NextAction("mc move from lava", ACTION_RAID + 1) }));
}

void RaidMcStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new GarrDisableDpsAoeMultiplier(botAI));
    multipliers.push_back(new BaronGeddonAbilityMultiplier(botAI));
    multipliers.push_back(new GolemaggMultiplier(botAI));
    multipliers.push_back(new MajordomoReflectionMultiplier(botAI));
}

void RaidMcStrategy::AppendTargetExclusions(GuidSet& exclusions, TargetValueExclusionType type)
{
    AiObjectContext* context = botAI->GetAiObjectContext();

    // CoA Magmadar summons two Core Hounds every 50 s. A hound that drops plays dead and comes back at full
    // health while another hound fights within 80 yd, and the next pair arrives before the last one is down:
    // they cannot be killed during the encounter and despawn with him. Skulling them (the trash rule) sent the
    // whole raid onto them for minutes until the tanks fell (NUC bench, 29/10, 8 wipes out of 8). Damage
    // dealers leave them to the off-tanks; the main tank keeps the body, his heads only pass hits on to it.
    if (AI_VALUE2(Unit*, "find target", "magmadar"))
    {
        bool const mainTank = PlayerbotAI::IsMainTank(botAI->GetBot());
        if (type == TargetValueExclusionType::DpsTarget || type == TargetValueExclusionType::DebuffTarget ||
            (type == TargetValueExclusionType::TankTarget && mainTank))
        {
            for (ObjectGuid const guid : AI_VALUE(GuidVector, "attackers"))
            {
                Unit* unit = botAI->GetUnit(guid);
                if (!unit)
                    continue;

                if (IsMcEntry(unit->GetEntry(), NPC_CORE_HOUND) ||
                    (type == TargetValueExclusionType::TankTarget && IsMagmadarHead(unit)))
                    exclusions.insert(guid);
            }
        }
    }

    if (type == TargetValueExclusionType::TankTarget ||
        (type == TargetValueExclusionType::Aoe && PlayerbotAI::IsTank(botAI->GetBot())))
        return;

    // Damage into these is wasted: Core Ragers are unkillable while Golemagg
    // lives (full heal at 50%). Tanks still pick them up through their own
    // target selection.
    bool const golemaggAlive = AI_VALUE2(Unit*, "find target", "golemagg the incinerator") != nullptr;
    // The DPS target search skips the moon (crowd control) mark, so a moon-marked healer does not count.
    Group const* group = botAI->GetBot()->GetGroup();
    ObjectGuid const moonGuid = group ? group->GetTargetIcon(RtiTargetValue::moonIndex) : ObjectGuid::Empty;
    bool flamewakerHealerAlive = false;
    GuidVector flamewakerElites;
    for (ObjectGuid const guid : AI_VALUE(GuidVector, "attackers"))
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        // CoA: Majordomo Executus is fought down to 20% (he then yields and turns friendly, which ends the
        // attack by itself); stock AzerothCore's "never attack him" stalled the encounter (raid report, 08/10).
        if (golemaggAlive && unit->GetEntry() == NPC_CORE_RAGER)
            exclusions.insert(guid);
        else if (IsMcEntry(unit->GetEntry(), NPC_FLAMEWAKER_HEALER) && unit->IsAlive() && guid != moonGuid)
            flamewakerHealerAlive = true;
        else if (IsMcEntry(unit->GetEntry(), NPC_FLAMEWAKER_ELITE))
            flamewakerElites.push_back(guid);
    }

    // Majordomo's healers first: they heal the elites and each other.
    if (type == TargetValueExclusionType::DpsTarget && flamewakerHealerAlive)
        exclusions.insert(flamewakerElites.begin(), flamewakerElites.end());
}
