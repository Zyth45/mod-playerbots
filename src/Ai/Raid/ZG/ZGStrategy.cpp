/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ZGStrategy.h"
#include "EncounterHelpers.h"
#include "Playerbots.h"
#include "ZGHelpers.h"
#include "ZGMultipliers.h"

using namespace ZulGurubHelpers;

namespace
{
bool IsThekalTrioMember(uint32 entry)
{
    return entry == static_cast<uint32>(ZulGurubNpcs::NPC_HIGH_PRIEST_THEKAL) ||
           entry == static_cast<uint32>(ZulGurubNpcs::NPC_ZEALOT_ZATH) ||
           entry == static_cast<uint32>(ZulGurubNpcs::NPC_ZEALOT_LORKHAN);
}
}  // namespace

void RaidZulGurubStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Hakkar
    triggers.push_back(new TriggerNode("hakkar mark", { NextAction("hakkar mark", ACTION_RAID) }));
    triggers.push_back(new TriggerNode("hakkar blood scythe", { NextAction("hakkar avoid blood scythe", ACTION_RAID + 1) }));
    triggers.push_back(new TriggerNode("hakkar blood pool", { NextAction("hakkar move from blood pool", ACTION_RAID + 1) }));
    // CoA Combat (lot 7, docs/ZG-ETAT-3.0.md): the poison pool during Blood Siphon (HK2), kill orders on Thekal's
    // zealots (TK1) and Jin'do's totems (JD1), Gri'lek's prey (EM1).
    triggers.push_back(new TriggerNode("hakkar siphon pool", { NextAction("hakkar move to siphon pool", ACTION_RAID + 2) }));
    triggers.push_back(new TriggerNode("thekal mark", { NextAction("thekal mark", ACTION_RAID) }));
    triggers.push_back(new TriggerNode("jindo mark", { NextAction("jindo mark", ACTION_RAID) }));
    triggers.push_back(new TriggerNode("grilek pursuit", { NextAction("grilek prey run", ACTION_RAID + 2) }));

    // High Priest Venoxis: his threat list is wiped when he turns into a snake; a tank takes him back at once,
    // and everyone else keeps out of Venom Spit's cone meanwhile.
    triggers.push_back(new TriggerNode("venoxis backup taunt", { NextAction("venoxis backup taunt", ACTION_RAID + 1) }));
    triggers.push_back(new TriggerNode("venoxis venom spit", { NextAction("venoxis avoid venom spit", ACTION_RAID + 1) }));

    // Bloodlord Mandokir
    triggers.push_back(new TriggerNode("bloodlord mandokir threatening gaze",
        { NextAction("bloodlord mandokir stop causing threat", ACTION_EMERGENCY + 6) }));
}

void RaidZulGurubStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new HakkarKeepPoisonousBloodMultiplier(botAI));
    multipliers.push_back(new BloodlordMandokirCauseNoThreatUnderGazeMultiplier(botAI));
    multipliers.push_back(new GrilekPreyMultiplier(botAI));
}

void RaidZulGurubStrategy::AppendTargetExclusions(GuidSet& exclusions, TargetValueExclusionType type)
{
    if (type != TargetValueExclusionType::DpsTarget && type != TargetValueExclusionType::DebuffTarget)
        return;

    Player* bot = botAI->GetBot();
    if (!EncounterHelpers::IsEncounterInProgress(bot, ZG_MAP_ID))
        return;

    // CoA Will of Hakkar: the controlled player absorbs everything for 15 s; damage on it is wasted.
    AiObjectContext* context = botAI->GetAiObjectContext();
    for (ObjectGuid const guid : AI_VALUE(GuidVector, "attackers"))
    {
        Unit* unit = botAI->GetUnit(guid);
        if (unit && (unit->HasAura(static_cast<uint32>(ZulGurubSpells::SPELL_WILL_OF_HAKKAR_CONTROL)) ||
                     unit->HasAura(static_cast<uint32>(ZulGurubSpells::SPELL_WILL_OF_HAKKAR_SHIELD))))
            exclusions.insert(guid);
    }

    // Thekal's first phase: a low member of the trio is left alone while another is still high.
    // CoA Combat (TK1): on CoA nobody of the trio comes back; a zealot left at 15 % keeps healing Thekal.
    if (ZgCoaFixesOn())
        return;
    std::vector<Unit*> trio;
    for (ObjectGuid const guid : AI_VALUE(GuidVector, "attackers"))
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !unit->IsAlive())
            continue;

        if (IsThekalTrioMember(unit->GetEntry()))
            trio.push_back(unit);
    }

    if (trio.size() < 2)
        return;

    bool anyHigh = false;
    for (Unit* unit : trio)
    {
        if (unit->GetHealthPct() > THEKAL_TRIO_SYNC_HEALTH_PCT)
            anyHigh = true;
    }

    if (!anyHigh)
        return;

    for (Unit* unit : trio)
    {
        if (unit->GetHealthPct() <= THEKAL_TRIO_SYNC_HEALTH_PCT)
            exclusions.insert(unit->GetGUID());
    }
}
