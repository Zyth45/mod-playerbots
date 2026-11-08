/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaPet.h"

#include "CoaAltAddon.h"
#include "CoaLootCompanionRules.h"
#include "CoaPetRules.h"
#include "Creature.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "SpellMgr.h"
#include "TemporarySummon.h"
#include "TrainerAction.h"
#include "GameTime.h"

#include <mutex>
#include <unordered_map>

using namespace CoaPet;

namespace
{
constexpr char const* LOOT_KEY = "coa_loot_pet";  // the loot pet's own choice (2.9.5)
constexpr char const* PET_KEY = "coa_pet";

CoaLootPetChoice LootChoice(Player* bot)
{
    uint32 const guid = bot->GetGUID().GetCounter();
    return CoaLootPetChoiceFrom(sRandomPlayerbotMgr.GetValue(guid, LOOT_KEY), sRandomPlayerbotMgr.GetData(guid, LOOT_KEY));
}

void StoreLoot(Player* bot, CoaLootPetChoice const& choice)
{
    sRandomPlayerbotMgr.SetPermanentValue(bot->GetGUID().GetCounter(), LOOT_KEY, CoaLootPetStoredValue(choice),
                                          CoaLootPetStoredData(choice));
}

Choice PetChoice(Player* bot) { return Load(sRandomPlayerbotMgr.GetData(bot->GetGUID().GetCounter(), PET_KEY)); }

void StorePet(Player* bot, Choice const& choice)
{
    sRandomPlayerbotMgr.SetPermanentValue(bot->GetGUID().GetCounter(), PET_KEY, 1, Store(choice));
}

Creature* Out(Player* bot)
{
    Map* map = bot->FindMap();
    if (!map || !bot->GetCritterGUID())
        return nullptr;
    Creature* critter = map->GetCreature(bot->GetCritterGUID());
    return critter && critter->IsAlive() && critter->GetOwnerGUID() == bot->GetGUID() ? critter : nullptr;
}

uint32 OutSpell(Player* bot)
{
    Creature* out = Out(bot);
    Companion const* c = out ? ByEntry(out->GetEntry()) : nullptr;
    return c ? c->spellId : 0;
}

std::string NameOf(Companion const& c)
{
    if (CreatureTemplate const* creature = sObjectMgr->GetCreatureTemplate(c.entry))
        if (!creature->Name.empty())
            return creature->Name;
    return c.name;
}

uint32 Favourite(Player* bot, Category category)
{
    return category == CAT_LOOT ? LootChoice(bot).favourite : PetChoice(bot).favourite[category];
}

void Reply(PlayerbotAI* botAI, std::string const& error)
{
    Player* bot = botAI->GetBot();
    if (!CoaAltAddon::AddonSeen(botAI))
    {
        // No addon: one plain line.
        std::string text = error.empty() ? "" : "Not now (" + error + "). ";
        text += "Companions:";
        bool any = false;
        for (Companion const& c : Companions())
            if (bot->HasSpell(c.spellId))
            {
                text += std::string(any ? ", " : " ") + NameOf(c) + " (" + CategoryName(c.category) + ")";
                any = true;
            }
        botAI->TellMaster(any ? text + "." : text + " none.");
        return;
    }

    if (!error.empty())
        CoaAltAddon::SendToMaster(botAI, ErrLine(error));
    for (Companion const& c : Companions())
        CoaAltAddon::SendToMaster(botAI, PetLine(c, bot->HasSpell(c.spellId), CoaAltAddon::Field(NameOf(c))));
    for (uint8 cat = 0; cat < CAT_COUNT; ++cat)
        CoaAltAddon::SendToMaster(botAI, FavLine(Category(cat), Favourite(bot, Category(cat))));
    CoaAltAddon::SendToMaster(botAI, OutLine(OutSpell(bot)));
    CoaAltAddon::SendToMaster(botAI, AutoLine(PetChoice(bot).autoOn));
    CoaAltAddon::SendToMaster(botAI, EndLine());
}

void Dismiss(Player* bot)
{
    if (Creature* out = Out(bot))
        if (TempSummon* summon = out->ToTempSummon())
            summon->UnSummon();
}

// ---------------------------------------------------------------- automatic use of the books ("pet auto on")

struct BookUse
{
    Category category = CAT_NONE;
    uint64 summonedAtMs = 0;
};

std::mutex autoLock;
std::unordered_map<uint64, BookUse> bookUses;         // alt -> the book it is about to use (map threads)
std::unordered_map<uint64, uint64> nextCraftCheckMs;  // alt -> next look at its professions

constexpr uint64 CRAFT_CHECK_MS = 10 * MINUTE * IN_MILLISECONDS;
constexpr uint64 BOOK_WAIT_MS = 2000;  // the book is out and in reach
constexpr uint64 BOOK_GIVE_UP_MS = 15000;

bool ProfessionAtRankCap(Player* bot)
{
    static uint32 const professions[] = {164, 165, 171, 182, 186, 197, 202, 333, 393, 755, 773, 129, 185, 356};
    for (uint32 skill : professions)
        if (bot->HasSkill(skill) && RankCapReached(bot->GetSkillValue(skill), bot->GetMaxSkillValue(skill)))
            return true;
    return false;
}

void Ask(Player* bot, Category category)
{
    std::lock_guard<std::mutex> guard(autoLock);
    bookUses.try_emplace(bot->GetGUID().GetRawValue(), BookUse{category, 0});
}
}  // namespace

namespace CoaPet
{
bool Handle(PlayerbotAI* botAI, Player* requester, std::string const& param)
{
    Command const cmd = Parse(param);
    if (cmd.verb == Verb::None)
        return false;
    if (!botAI)
        return true;
    Player* bot = botAI->GetBot();
    Player* master = botAI->GetMaster();
    // An alt's own companions answer its master only (like "loot pet").
    if (!bot || !master || requester != master || !botAI->IsAltBot())
        return true;

    if (!cmd.valid)
    {
        if (CoaAltAddon::AddonSeen(botAI))
            Reply(botAI, "unknown");
        else
            botAI->TellMaster("I do not know that companion. Usage: pet list | set <loot|quest|class|craft> <spell id|0> "
                              "| use <category|spell id> | dismiss | auto on|off");
        return true;
    }

    std::string error;
    switch (cmd.verb)
    {
        case Verb::List:
            break;
        case Verb::Set:
            if (cmd.spellId && !bot->HasSpell(cmd.spellId))
                error = "unknown";
            else if (cmd.category == CAT_LOOT)
            {
                // The loot favourite is the loot pet's: "pet set loot <id>" and "loot pet <id>" are one setting.
                CoaLootPetChoice loot = LootChoice(bot);
                loot.favourite = cmd.spellId;
                loot.off = false;
                StoreLoot(bot, loot);
            }
            else
            {
                Choice choice = PetChoice(bot);
                choice.favourite[cmd.category] = cmd.spellId;
                StorePet(bot, choice);
            }
            break;
        case Verb::Use:
        {
            uint32 const spellId = cmd.spellId ? cmd.spellId
                                               : Choose(cmd.category, Favourite(bot, cmd.category),
                                                        [bot](uint32 s) { return bot->HasSpell(s); });
            Map* map = bot->FindMap();
            error = Refusal(spellId && bot->HasSpell(spellId), bot->IsAlive(), bot->IsInCombat(), bot->IsMounted(),
                            cmd.category == CAT_LOOT && map && map->IsRaid());
            if (error.empty() && OutSpell(bot) != spellId)
            {
                Dismiss(bot);
                // Triggered, as the loot pet: an alt following its master is nearly always moving.
                if (!sSpellMgr->GetSpellInfo(spellId) || bot->CastSpell(bot, spellId, true) != SPELL_CAST_OK)
                    error = "cast";
            }
            break;
        }
        case Verb::Dismiss:
            Dismiss(bot);
            break;
        case Verb::Auto:
        {
            Choice choice = PetChoice(bot);
            choice.autoOn = cmd.on;
            StorePet(bot, choice);
            // The loot pet is the one the alt brings out by itself today: auto off keeps it in.
            CoaLootPetChoice loot = LootChoice(bot);
            loot.off = !cmd.on;
            StoreLoot(bot, loot);
            if (!cmd.on)
                Dismiss(bot);
            break;
        }
        default:
            break;
    }
    Reply(botAI, error);
    return true;
}

void OnLevelUp(Player* alt)
{
    PlayerbotAI* botAI = alt ? GET_PLAYERBOT_AI(alt) : nullptr;
    if (!botAI || !botAI->IsAltBot())
        return;
    Choice const choice = PetChoice(alt);
    if (choice.autoOn && Choose(CAT_CLASS, choice.favourite[CAT_CLASS], [alt](uint32 s) { return alt->HasSpell(s); }))
        Ask(alt, CAT_CLASS);
}

void Tick(PlayerbotAI* botAI)
{
    Player* bot = botAI ? botAI->GetBot() : nullptr;
    if (!bot || !bot->IsInWorld() || !botAI->IsAltBot())
        return;
    uint64 const key = bot->GetGUID().GetRawValue();
    uint64 const now = GameTime::GetGameTimeMS().count();

    BookUse use;
    {
        std::lock_guard<std::mutex> guard(autoLock);
        auto it = bookUses.find(key);
        if (it != bookUses.end())
            use = it->second;
        else
        {
            // Now and then, the professions: one at the top of its rank asks for the Book of Artisans.
            uint64& next = nextCraftCheckMs[key];
            if (now < next)
                return;
            next = now + CRAFT_CHECK_MS;
        }
    }

    if (use.category == CAT_NONE)
    {
        Choice const choice = PetChoice(bot);
        if (choice.autoOn && ProfessionAtRankCap(bot) &&
            Choose(CAT_CRAFT, choice.favourite[CAT_CRAFT], [bot](uint32 s) { return bot->HasSpell(s); }))
            Ask(bot, CAT_CRAFT);
        return;
    }

    auto done = [key]()
    {
        std::lock_guard<std::mutex> guard(autoLock);
        bookUses.erase(key);
    };
    if (!PetChoice(bot).autoOn)
        return done();
    // Not now: it waits for a calm moment.
    Map* map = bot->FindMap();
    if (!bot->IsAlive() || bot->IsInCombat() || bot->IsMounted() || bot->IsFlying() || bot->IsInFlight() || !map ||
        bot->InBattleground() || bot->InArena())
        return;

    if (!use.summonedAtMs)
    {
        uint32 const spellId =
            Choose(use.category, PetChoice(bot).favourite[use.category], [bot](uint32 s) { return bot->HasSpell(s); });
        if (!spellId || !sSpellMgr->GetSpellInfo(spellId))
            return done();
        Dismiss(bot);
        if (bot->CastSpell(bot, spellId, true) != SPELL_CAST_OK)
            return done();
        std::lock_guard<std::mutex> guard(autoLock);
        bookUses[key].summonedAtMs = now;
        return;
    }

    if (now < use.summonedAtMs + BOOK_WAIT_MS)
        return;
    Creature* book = Out(bot);
    Companion const* c = book ? ByEntry(book->GetEntry()) : nullptr;
    if (c && c->category == use.category)
    {
        TrainerAction trainer(botAI);
        trainer.LearnFrom(book);
        Dismiss(bot);
        return done();
    }
    if (now > use.summonedAtMs + BOOK_GIVE_UP_MS)
        return done();
}
}  // namespace CoaPet
