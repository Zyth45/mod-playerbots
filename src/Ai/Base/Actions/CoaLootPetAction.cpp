/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaLootPetAction.h"

#include "AscensionCompanionLoot.h"
#include "CoaAltAddon.h"
#include "CoaLootCompanionRules.h"
#include "Creature.h"
#include "DBCStores.h"
#include "Event.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Playerbots.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "TemporarySummon.h"
#include "Timer.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace
{
constexpr char const* CHOICE_KEY = "coa_loot_pet";
constexpr size_t TEXT_LIST_MAX = 15;

std::mutex lastCheckLock;
std::unordered_map<ObjectGuid::LowType, uint32> lastCheck;

// The creature a companion spell brings out (a SUMMON_TYPE_MINIPET summon, which the core sets as the critter), 0
// for any other spell.
uint32 CritterEntry(SpellInfo const* info)
{
    for (SpellEffectInfo const& effect : info->Effects)
        if (effect.Effect == SPELL_EFFECT_SUMMON)
            if (SummonPropertiesEntry const* properties = sSummonPropertiesStore.LookupEntry(effect.MiscValueB))
                if (properties->Type == SUMMON_TYPE_MINIPET && effect.MiscValue > 0 &&
                    sObjectMgr->GetCreatureTemplate(uint32(effect.MiscValue)))
                    return uint32(effect.MiscValue);
    return 0;
}

std::vector<CoaKnownCompanion> KnownCompanions(Player* bot)
{
    std::vector<CoaKnownCompanion> known;
    for (auto const& [spellId, spell] : bot->GetSpellMap())
    {
        if (spell->State == PLAYERSPELL_REMOVED || !spell->Active)
            continue;
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
        if (uint32 const entry = info ? CritterEntry(info) : 0)
            known.push_back({spellId, entry, Ascension::CompanionLooting::IsSelfLootingCompanion(entry)});
    }
    std::sort(known.begin(), known.end(),
              [](CoaKnownCompanion const& a, CoaKnownCompanion const& b) { return a.spellId < b.spellId; });
    return known;
}

std::string CompanionName(CoaKnownCompanion const& companion)
{
    if (CreatureTemplate const* creature = sObjectMgr->GetCreatureTemplate(companion.creatureEntry))
        if (!creature->Name.empty())
            return creature->Name;
    SpellInfo const* info = sSpellMgr->GetSpellInfo(companion.spellId);
    return info && info->SpellName[0] ? std::string(info->SpellName[0]) : std::to_string(companion.spellId);
}

CoaLootPetChoice ChoiceOf(Player* bot)
{
    ObjectGuid::LowType const guid = bot->GetGUID().GetCounter();
    return CoaLootPetChoiceFrom(sRandomPlayerbotMgr.GetValue(guid, CHOICE_KEY),
                                sRandomPlayerbotMgr.GetData(guid, CHOICE_KEY));
}

void Store(Player* bot, CoaLootPetChoice const& choice)
{
    sRandomPlayerbotMgr.SetPermanentValue(bot->GetGUID().GetCounter(), CHOICE_KEY, CoaLootPetStoredValue(choice),
                                          CoaLootPetStoredData(choice));
}

Creature* OwnedCritter(Player* bot)
{
    Map* map = bot->FindMap();
    if (!map || !bot->GetCritterGUID())
        return nullptr;
    Creature* critter = map->GetCreature(bot->GetCritterGUID());
    return critter && critter->IsAlive() && critter->GetOwnerGUID() == bot->GetGUID() ? critter : nullptr;
}

// A stealth or invisibility the alt is actively using (CoaStealthBlocksCompanion), not a passive or permanent one.
bool Sneaking(Player* bot)
{
    for (AuraType type : {SPELL_AURA_MOD_STEALTH, SPELL_AURA_MOD_INVISIBILITY})
        for (AuraEffect const* effect : bot->GetAuraEffectsByType(type))
            if (Aura const* aura = effect->GetBase())
                if (CoaStealthBlocksCompanion(aura->IsPassive(), aura->IsPermanent()))
                    return true;
    return false;
}

bool DueForCheck(Player* bot)
{
    std::lock_guard<std::mutex> guard(lastCheckLock);
    auto [itr, inserted] = lastCheck.try_emplace(bot->GetGUID().GetCounter(), getMSTime());
    if (inserted)
        return false;
    if (getMSTimeDiff(itr->second, getMSTime()) < CoaLootCompanionCheckMs)
        return false;
    itr->second = getMSTime();
    return true;
}

void SendList(PlayerbotAI* botAI, std::vector<CoaKnownCompanion> const& known, CoaLootPetChoice const& choice)
{
    for (CoaKnownCompanion const& companion : CoaListedCompanions(known, choice.favourite))
        CoaAltAddon::SendToMaster(botAI, CoaLootPetLine(companion, CoaAltAddon::Field(CompanionName(companion))));
    CoaAltAddon::SendToMaster(botAI, CoaLootPetAppLine(Ascension::CompanionLooting::HasLootAppearance(botAI->GetBot())));
    CoaAltAddon::SendToMaster(botAI, CoaLootPetFavLine(choice));
    CoaAltAddon::SendToMaster(botAI, CoaLootPetEndLine());
}

std::string StatusText(Player* bot, std::vector<CoaKnownCompanion> const& known, CoaLootPetChoice const& choice)
{
    std::string text = "Loot pet: ";
    if (!sPlayerbotAIConfig.altLootCompanion)
        text += "disabled on this realm (AiPlayerbot.AltLootCompanion = 0)";
    else if (choice.off)
        text += "off";
    else if (CoaKnownCompanion const* fav = choice.favourite ? CoaFindCompanion(known, choice.favourite) : nullptr)
        text += "favourite " + CompanionName(*fav);
    else
        text += "automatic";
    text += Ascension::CompanionLooting::HasLootAppearance(bot) ? ", Loot-Transfigurator active" : "";
    text += Ascension::CompanionLooting::CompanionLoots(bot) ? ", my companion loots for me." : ".";
    return text;
}

std::string ListText(std::vector<CoaKnownCompanion> const& all, std::uint32_t favourite)
{
    std::vector<CoaKnownCompanion> const known = CoaListedCompanions(all, favourite);
    if (known.empty())
        return "I know no companion that loots by itself.";
    std::string text = "My looting companions:";
    for (size_t i = 0; i < known.size() && i < TEXT_LIST_MAX; ++i)
        text += (i ? ", " : " ") + CompanionName(known[i]) + " (" + std::to_string(known[i].spellId) +
                (known[i].selfLoots ? ", loots" : "") + ")";
    if (known.size() > TEXT_LIST_MAX)
        text += " and " + std::to_string(known.size() - TEXT_LIST_MAX) + " more";
    return text + ".";
}

uint32 SpellIdOf(Player* master, std::string arg)
{
    arg.erase(0, std::min(arg.find_first_not_of(' '), arg.size()));
    arg.erase(arg.find_last_not_of(' ') + 1);
    if (!arg.empty() && arg.size() < 10 && arg.find_first_not_of("0123456789") == std::string::npos)
        return uint32(std::stoul(arg));
    PlayerbotChatHandler handler(master);
    return handler.extractSpellId(arg);
}
}  // namespace

bool CoaLootPetAction::Execute(Event event)
{
    Player* master = GetMaster();
    Player* requester = event.getOwner() ? event.getOwner() : master;
    if (!master || requester != master || !botAI->IsAltBot())
        return false;

    std::string const arg = event.getParam();
    std::vector<CoaKnownCompanion> const known = KnownCompanions(bot);
    CoaLootPetChoice choice = ChoiceOf(bot);
    bool const addon = CoaAltAddon::AddonSeen(botAI);

    switch (CoaLootPetVerbOf(arg))
    {
        case CoaLootPetVerb::Status:
            break;
        case CoaLootPetVerb::List:
            if (addon)
                SendList(botAI, known, choice);
            else
                botAI->TellMaster(ListText(known, choice.favourite));
            return true;
        case CoaLootPetVerb::Off:
        {
            choice.off = true;
            Store(bot, choice);
            Creature* out = OwnedCritter(bot);
            if (CoaLootPetDismissNow(CoaLootPetVerb::Off, out != nullptr))
                if (TempSummon* summon = out->ToTempSummon())
                    summon->UnSummon();
            break;
        }
        case CoaLootPetVerb::Auto:
            choice = CoaLootPetChoice();
            Store(bot, choice);
            break;
        case CoaLootPetVerb::Set:
        {
            uint32 const spellId = SpellIdOf(master, arg);
            if (!spellId || !CoaFindCompanion(known, spellId))
            {
                if (addon)
                    SendList(botAI, known, choice);
                else
                    botAI->TellMaster("I do not know that companion. Usage: loot pet [list | <spell id> | off | auto]");
                return false;
            }
            choice.favourite = spellId;
            choice.off = false;
            Store(bot, choice);
            break;
        }
        default:
            return false;
    }

    if (addon)
        SendList(botAI, known, choice);
    else
        botAI->TellMaster(StatusText(bot, known, choice));
    return true;
}

void CoaLootCompanionTick(PlayerbotAI* botAI)
{
    if (!botAI)
        return;
    Player* bot = botAI->GetBot();
    if (!bot || !bot->IsInWorld() || !botAI->IsAltBot() || !DueForCheck(bot))
        return;
    // One WARN line per check (every CoaLootCompanionCheckMs) while the alt brings out no loot pet, with the reason.
    auto skip = [bot](std::string const& reason)
    {
        LOG_WARN("playerbots", "coa loot pet: {} brings out none: {}", bot->GetName(), reason);
    };
    if (!sPlayerbotAIConfig.altLootCompanion)
        return skip("AiPlayerbot.AltLootCompanion = 0");
    Map* map = bot->FindMap();
    if (!map)
        return skip("no map");

    bool const lootAppearance = Ascension::CompanionLooting::HasLootAppearance(bot);
    CoaLootCompanionMoment const moment{bot->IsAlive(),
                                        bot->IsInCombat(),
                                        bot->IsMounted(),
                                        bot->IsFlying(),
                                        bot->InBattleground() || bot->InArena(),
                                        map->IsRaid(),
                                        lootAppearance,
                                        bot->IsNonMeleeSpellCast(true) || bot->IsInFlight() || bot->GetVehicle() ||
                                            bot->HasUnitState(UNIT_STATE_CONTROLLED) || Sneaking(bot)};
    if (!CoaLootCompanionMomentOk(moment))
        return skip(std::string("not now:") + (moment.alive ? "" : " dead") + (moment.inCombat ? " combat" : "") +
                    (moment.mounted ? " mounted" : "") + (moment.flying ? " flying" : "") +
                    (moment.inBattlegroundOrArena ? " battleground" : "") +
                    (moment.inRaid && !lootAppearance ? " raid without the appearance" : "") +
                    (bot->IsNonMeleeSpellCast(true) ? " casting" : "") + (bot->IsInFlight() ? " taxi" : "") +
                    (bot->GetVehicle() ? " vehicle" : "") + (bot->HasUnitState(UNIT_STATE_CONTROLLED) ? " controlled" : "") +
                    (Sneaking(bot) ? " stealthed" : ""));

    CoaLootPetChoice const choice = ChoiceOf(bot);
    if (choice.off)
        return skip("loot pet off for this alt");

    Creature* out = OwnedCritter(bot);
    bool const outLoots = out && Ascension::CompanionLooting::CompanionLoots(bot);
    if (outLoots && !choice.favourite)
        return;

    std::vector<CoaKnownCompanion> const known = KnownCompanions(bot);
    uint32 const chosen = CoaChooseLootCompanion(known, choice.favourite, lootAppearance, moment.inRaid);
    CoaKnownCompanion const* companion = chosen ? CoaFindCompanion(known, chosen) : nullptr;
    if (!companion)
        return skip("no looting companion known (" + std::to_string(known.size()) + " companions known, favourite " +
                    std::to_string(choice.favourite) + ", appearance " + (lootAppearance ? "on" : "off") + ")");
    if (!CoaLootCompanionNeedsSummon(companion->creatureEntry, chosen == choice.favourite, out ? out->GetEntry() : 0,
                                     outLoots))
        return;

    SpellInfo const* info = sSpellMgr->GetSpellInfo(chosen);
    if (!info)
        return skip("no spell " + std::to_string(chosen));
    // Triggered: an alt following its master is nearly always moving, and a cast-time summon never got through.
    SpellCastResult const result = bot->CastSpell(bot, chosen, true);
    LOG_WARN("playerbots", "coa loot pet: {} summons creature {} with spell {} (out before: {}): cast result {}",
             bot->GetName(), companion->creatureEntry, chosen, out ? out->GetEntry() : 0, uint32(result));
}

bool CoaLeavesCorpseToCompanion(Player* bot, Creature* corpse, bool skinning)
{
    if (!sPlayerbotAIConfig.altLootCompanion || !bot || !corpse)
        return false;
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI || !botAI->IsAltBot())
        return false;
    Creature* companion = OwnedCritter(bot);
    if (!companion || !companion->IsInMap(corpse))
        return false;
    bool const loots = skinning ? Ascension::CompanionLooting::CompanionSkins(bot)
                                : Ascension::CompanionLooting::CompanionLoots(bot);
    float const radius = skinning ? Ascension::CompanionLooting::SkinningRadius(bot)
                                  : Ascension::CompanionLooting::LootRadius(bot);
    float const botToCorpse = bot->GetDistance(corpse);
    float const companionToCorpse = companion->GetDistance(corpse);
    float const companionToBot = companion->GetDistance(bot);
    // The sight line last: it is the costly test.
    return CoaLeaveCorpseToCompanion(true, loots, radius, botToCorpse, companionToCorpse, companionToBot, true) &&
           companion->IsWithinLOSInMap(corpse);
}
