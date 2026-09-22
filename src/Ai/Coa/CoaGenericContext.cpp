/*
 * CoA: implementations of the generic, parameterised triggers.
 * The logic is deliberately identical to the originals in GenericTriggers.cpp -
 * the only difference is that the spell name comes from the qualifier instead
 * of from the const field AiNamedObject::name.
 */
#include "CoaGenericContext.h"

#include "mod-ascension-compat/src/AscensionSpecialization.h"

#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Playerbots.h"
#include "CoaSpecialization.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"
#include <map>
#include <cctype>
#include <algorithm>

// The dispel type of a cure spell, taken from the spell itself.
//
// SPELL_EFFECT_DISPEL carries the type as its MiscValue - 1 magic, 2 curse,
// 3 disease, 4 poison. Dispel Magic (527) is the calibration case: effect 38,
// MiscValue 1.
//
// The name is resolved against the bot's own spellbook, so a spell the bot has
// not learned yields 0, and so does a spell that dispels nothing.
uint32 CoaDispelTypeOf(PlayerbotAI* botAI, std::string const& spellName)
{
    if (spellName.empty())
        return 0;

    uint32 const spellId =
        botAI->GetAiObjectContext()->GetValue<uint32>("spell id", spellName)->Get();
    if (!spellId)
        return 0;

    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
    if (!info)
        return 0;

    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        if (info->Effects[i].Effect == SPELL_EFFECT_DISPEL)
            return uint32(info->Effects[i].MiscValue);

    return 0;
}

bool CoaHasAuraTrigger::IsActive()
{
    if (qualifier.empty())
        return false;
    // as in HasAuraTrigger::IsActive
    return botAI->HasAura(qualifier, GetTarget(), false, false, -1, true);
}

bool CoaTargetHasAuraTrigger::IsActive()
{
    if (qualifier.empty())
        return false;
    Unit* target = GetTarget();
    return target && botAI->HasAura(qualifier, target, false, false, -1, true);
}

bool CoaHasNoAuraTrigger::IsActive()
{
    if (qualifier.empty())
        return false;
    // as in HasNoAuraTrigger::IsActive
    return !botAI->HasAura(qualifier, GetTarget());
}

std::string CoaAuraStacksTrigger::SpellPart() const
{
    size_t comma = qualifier.rfind(',');
    return comma == std::string::npos ? qualifier : qualifier.substr(0, comma);
}

int CoaAuraStacksTrigger::StackPart() const
{
    size_t comma = qualifier.rfind(',');
    if (comma == std::string::npos)
        return 1;
    return atoi(qualifier.c_str() + comma + 1);
}

bool CoaAuraStacksTrigger::IsActive()
{
    std::string const spellName = SpellPart();
    if (spellName.empty())
        return false;
    // as in HasAuraStackTrigger::IsActive
    // checkDuration MUST be false.
    //
    // With checkDuration=true PlayerbotAI::GetAura skips every aura whose
    // GetDuration() is -1, that is every PERMANENT one. Class resources are
    // exactly that: Felfury, Spirit, Insanity and the rest stack indefinitely
    // until they are spent.
    //
    // With true the trigger therefore NEVER fired. It became visible in game on
    // 15 Sep 2026: the Felsworn cast neither Azzinoth's Assault nor Sargeron
    // Smite (both `aura stacks::Felfury,2`), and Spirit Eclipse never happened
    // on the Witch Doctor.
    return botAI->GetAura(spellName, GetTarget(), false, false, StackPart()) != nullptr;
}

std::string CoaResourceTrigger::NamePart() const
{
    size_t comma = qualifier.rfind(',');
    return comma == std::string::npos ? qualifier : qualifier.substr(0, comma);
}

int CoaResourceTrigger::PercentPart() const
{
    size_t comma = qualifier.rfind(',');
    if (comma == std::string::npos)
        return 100;
    return atoi(qualifier.c_str() + comma + 1);
}

// The core resources under their own names. The compatibility module only
// carries its OWN resources (Insanity, Static, Solar Power and so on) - rage,
// energy, runic power, focus and mana come from the core and were not available
// to this trigger at all.
//
// It showed on 15 Sep 2026: Command: Undead costs 30 runic power, so the
// Necromancer bot could only cast the spender late, and we had no way to put it
// behind its builder. Same thing on the Primalist with rage.
static std::map<std::string, Powers> const CORE_RESOURCES = {
    {"mana", POWER_MANA},
    {"rage", POWER_RAGE},
    {"focus", POWER_FOCUS},
    {"energy", POWER_ENERGY},
    {"runicpower", POWER_RUNIC_POWER},
    {"runic power", POWER_RUNIC_POWER},
};

bool CoaResourceTrigger::IsActive()
{
    std::string const name = NamePart();
    if (name.empty())
        return false;

    // The Ascension-specific resources (Static, Felfury, Insanity, Solar Power
    // and the rest) are not reachable: mod-ascension-compat's public API
    // exposes specializations, talents and class abilities, but nothing to read
    // a custom resource. So only the core powers below work here.
    //
    // No rotation currently needs more - all 71 of them get by on aura stacks,
    // buff missing, debuff missing and can cast. If that changes, the shortest
    // route is asking for a percent accessor in AscensionSpecialization.h.

    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    auto itr = CORE_RESOURCES.find(lower);
    if (itr == CORE_RESOURCES.end())
        return false;  // Neither a module nor a core resource of that name

    Powers const type = itr->second;
    if (!bot->HasActivePowerType(type))
        return false;  // The class does not carry this resource

    uint32 const maximum = bot->GetMaxPower(type);
    if (!maximum)
        return false;

    return int32(bot->GetPower(type) * 100 / maximum) >= PercentPart();
}

// Wide enough for a bot to see its summons anywhere in a fight, and narrow
// enough that creatures at the other end of the zone do not count.
static constexpr float SEARCH_RANGE = 60.0f;

bool CoaBuffMissingTrigger::IsActive()
{
    bool active = BuffTrigger::IsActive();
    if (active)
    {
        uint32 const id = AI_VALUE2(uint32, "spell id", spell);
        SpellInfo const* info = id ? sSpellMgr->GetSpellInfo(id) : nullptr;
        active = !CoaHealerAvoidsForm(bot, info) && !CoaHoldsExclusiveSibling(bot, info);
    }
    return backoff.Allow(active);
}

bool CoaDebuffMissingTrigger::IsActive()
{
    bool active = DebuffTrigger::IsActive();
    if (active)
    {
        uint32 const id = AI_VALUE2(uint32, "spell id", spell);
        active = !CoaHealerSavesManaFrom(bot, id ? sSpellMgr->GetSpellInfo(id) : nullptr);
    }
    return backoff.Allow(active);
}

bool CoaCanCastTrigger::IsActive()
{
    if (!SpellCanBeCastTrigger::IsActive())
        return false;

    uint32 const id = AI_VALUE2(uint32, "spell id", spell);
    SpellInfo const* info = id ? sSpellMgr->GetSpellInfo(id) : nullptr;
    if (!info)
        return true;

    int32 const duration = info->GetMaxDuration();
    if (bot->HasAura(id) && (duration < 0 || duration > 60 * IN_MILLISECONDS))
        return false;

    bool summons = false;
    for (SpellEffectInfo const& effect : info->Effects)
    {
        // Travel utility has no place in a rotation: Grace of the Moon, a water walk at 40% of base
        // mana that any damage cancels, took 79% of a Starcaller healer's mana in one fight.
        if (effect.Effect == SPELL_EFFECT_APPLY_AURA || effect.Effect == SPELL_EFFECT_APPLY_AREA_AURA_PARTY ||
            effect.Effect == SPELL_EFFECT_APPLY_AREA_AURA_RAID)
            switch (effect.ApplyAuraName)
            {
                case SPELL_AURA_WATER_WALK: case SPELL_AURA_FEATHER_FALL: case SPELL_AURA_HOVER:
                case SPELL_AURA_WATER_BREATHING:
                    return false;
                default:
                    break;
            }
        if (effect.Effect == SPELL_EFFECT_SUMMON)
            summons = true;
    }

    // A ward or effigy of which only one may stand: not again while the bot's own still stands
    // (Healing Ward was put down 15 times in one fight, 18% of base mana each).
    if (summons && getMSTimeDiff(summonCheckedAt, getMSTime()) < 3 * IN_MILLISECONDS && summonCheckedAt)
    {
        if (summonStanding)
            return false;
    }
    else if (summons)
    {
        summonCheckedAt = getMSTime();
        summonStanding = false;
        std::list<Unit*> nearby;
        Acore::AnyUnitInObjectRangeCheck check(bot, SEARCH_RANGE);
        Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> search(bot, nearby, check);
        Cell::VisitObjects(bot, search, SEARCH_RANGE);
        for (Unit* unit : nearby)
            if (unit && unit->IsAlive() && !unit->IsPlayer() && unit->GetUInt32Value(UNIT_CREATED_BY_SPELL) == id &&
                (unit->GetOwnerGUID() == bot->GetGUID() || unit->GetCreatorGUID() == bot->GetGUID()))
            {
                summonStanding = true;
                return false;
            }
    }

    return !CoaHealerSavesManaFrom(bot, info) && !CoaHealerAvoidsForm(bot, info) &&
           !CoaHoldsExclusiveSibling(bot, info);
}

bool CoaSummonMissingTrigger::IsActive()
{
    for (Unit* unit : bot->m_Controlled)
        if (unit && unit->IsAlive() && unit->GetOwnerGUID() == bot->GetGUID())
            return false;

    // NOT through AI_VALUE("nearest npcs"): that value is cached. Freshly
    // summoned creatures only appear in it after the next refresh, and during
    // that window the trigger keeps the way clear - which is how the Witch
    // Doctor put down a whole field of Serpent Wards on 15 Sep 2026. Hence our
    // own, uncached search here.
    std::list<Unit*> nearby;
    Acore::AnyUnitInObjectRangeCheck check(bot, SEARCH_RANGE);
    Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> search(bot, nearby, check);
    Cell::VisitObjects(bot, search, SEARCH_RANGE);

    for (Unit* unit : nearby)
        if (unit && unit->IsAlive() && !unit->IsPlayer() &&
            unit->GetOwnerGUID() == bot->GetGUID())
            return false;

    return true;
}

bool CoaSpellReadyTrigger::IsActive()
{
    if (qualifier.empty())
        return false;
    // as in SpellNoCooldownTrigger::IsActive
    uint32 spellId = AI_VALUE2(uint32, "spell id", qualifier);
    if (!spellId)
        return false;

    return !bot->HasSpellCooldown(spellId);
}
