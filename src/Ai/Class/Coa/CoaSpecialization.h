/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COASPECIALIZATION_H
#define _PLAYERBOT_COASPECIALIZATION_H

#include "Define.h"
#include "ObjectGuid.h"

#include <set>
#include <string>

namespace AscensionCompatData
{
struct CoATalentEntry;
}

class Channel;
class Group;
class Player;
class PlayerbotAI;
class SpellInfo;
class Unit;

enum class CoaRole : uint8
{
    Dps,
    Tank,
    Heal
};

// How a CoA character fights, from the ascensionsidekick.com role of its specialization.
enum class CoaStyle : uint8
{
    Melee,   // strikes in melee range
    Ranged,  // physical damage from range: bows, guns, thrown weapons
    Caster   // spells from range
};

// Primary stats of a CoA specialization, as a mask.
enum CoaStat : uint8
{
    COA_STAT_STRENGTH  = 0x01,
    COA_STAT_AGILITY   = 0x02,
    COA_STAT_INTELLECT = 0x04,
    COA_STAT_SPIRIT    = 0x08,
    COA_STAT_STAMINA   = 0x10
};

// Armor a CoA class is proficient with. CoA grants every armor proficiency of a class at
// character creation (acore_world.playercreateinfo_spell_custom, required level 0), so this
// never depends on the character's level, unlike the level 40 step of the WotLK classes.
struct CoaArmorProficiency
{
    uint8 heaviestArmor;  // ITEM_SUBCLASS_ARMOR_*
    bool usesShield;
};

// Armor proficiencies of one of the 21 CoA classes, or nullptr for any other class.
CoaArmorProficiency const* GetCoaArmorProficiency(uint8 playerClass);

// Role of a Conquest of Azeroth character, from its active specialization. Dps when it has none.
CoaRole GetCoaRole(Player const* player);

// Whether a CoA healer holding its mana back for heals should leave this spell alone: it costs mana
// and the healer is under its reserve. For the authored rotations' "can cast" lines, which would
// otherwise spend on damage and buffs every mana the heal logic keeps (healer trial, 22/09/2026:
// 43 to 58% of a healer's mana went into one such spell).
bool CoaHealerSavesManaFrom(Player* bot, SpellInfo const* info);

// Whether this is a form a CoA healer must not take: its heals cannot be cast in it (a Venomancer
// healer's rotation put Spider Form back 10 times in one fight, each time the healer left it to heal).
bool CoaHealerAvoidsForm(Player* bot, SpellInfo const* info);

// Whether the bot already carries another spell of which only one may be active ("Can only have 1
// Skin active at a time", "Only 1 Ascension spell can be active"): the rotations list them all, and
// each one cast removed the other (Pyromancer skins, 23 casts a fight).
bool CoaHoldsExclusiveSibling(Player* bot, SpellInfo const* info);

// For a rotation line ("buff missing", "can cast"): whether the bot may cast this spell now as far
// as exclusive families go. Besides CoaHoldsExclusiveSibling, with AiPlayerbot.CoaExclusiveFamilies a
// family the world database makes exclusive (spell_group_stack_rules: Stormbringer Aegis and
// Pressures, Barbarian shouts, Starcaller Aspects) keeps the member named by the highest priority line
// of the rotation: the other lines of the family wait until the bot wears none of it, where each line
// recast its buff in turn (Shocking and Tempest Aegis, Barometric and Atmospheric Pressure, report of
// 02/10). The kept member replaces a sibling of the bot's own, one "coa buff" put on, never one of
// another caster.
bool CoaRotationMayCast(PlayerbotAI* botAI, Player* bot, SpellInfo const* info);

// Whether a bot that is no tank must leave this spell alone: a taunt, or a buff raising the threat of the
// bot itself (Dogsmeller, 07/10: a damage dealer taunting the mobs off the tank). Every cast passes here.
bool CoaTankOnlySpell(Player* bot, SpellInfo const* info, Unit* target);

// Whether the target already wears a rival buff of this one from the bot (a caster keeps one of them on each target).
bool CoaRivalBuffBlocks(Player* bot, SpellInfo const* info, Unit* target);

// Whether this spell would carry the bot, in a fight, away from where it fights: a roll or run it cannot stop
// (Boulder Dash), or a rush to a place ahead while it stands in reach of its enemy (Primal Rush). A healer
// never casts a spell that moves it (a jump, leap, charge or blink: Dragon Leap), in a fight or out of it.
bool CoaCarriesBotAway(Player* bot, SpellInfo const* info);

// A tank out of combat puts on the threat buffs it knows outside its class kit (every 10 seconds).
void CoaTankThreatBuffs(PlayerbotAI* botAI, Player* bot);

// A tank bot wears Increased Threat at AiPlayerbot.CoaTankThreatBonus percent; a bot that is no tank loses it.
void CoaTankThreatBonus(Player* bot);

// The heals a CoA healer considers, for the fight log: "Med Pack (502534), ...".
std::string CoaHealKit(Player* bot);

// Fighting style and primary stats of a CoA character's specialization. Before it has one
// (under level 10), those shared by most specializations of its class.
CoaStyle GetCoaStyle(Player const* player);
uint8 GetCoaPrimaryStats(Player const* player);

// Gives a random bot of a CoA class a specialization once it reaches level 10, the way a
// player picks one in the client. Returns true when a specialization was just chosen.
bool EnsureCoaSpecialization(Player* bot);

// Spends a random bot's Character Advancement points the way a player of its specialization
// would: one ability essence at every even level from 10 (class tree), one talent essence at every
// odd level from 11 (specialization tree), in the order of the ascensionsidekick.com level build.
// Returns the number of entries raised.
uint32 ApplyCoaTalents(Player* bot);

// The Character Advancement entry `entryId`, or null.
AscensionCompatData::CoATalentEntry const* FindCoaEntry(uint32 entryId);

// The paid entries of the bot's class it may hold now: its class tree and its active specialization's tree.
bool CoaEntryOffered(Player* bot, AscensionCompatData::CoATalentEntry const& entry);

// Class tree points (AE) and specialization tree points (TE): available at the bot's level, and spent.
struct CoaPoints
{
    uint32 classBudget = 0, specBudget = 0, classSpent = 0, specSpent = 0;
};
CoaPoints PointsOf(Player* bot);

// Why `rank` of this entry cannot be taken now, or empty: the rules the core applies to a player
// learning it from the talent frame (AscensionCompat SetTalentRank), which SetAscensionTalentRank leaves out.
std::string RefusalFor(Player* bot, AscensionCompatData::CoATalentEntry const& entry, uint32 rank);

// Spends the free Character Advancement points of a player's own character (an alt played through the bot AI)
// along its specialization's level build, the way its owner could in the talent frame: one rank at a time, each
// under RefusalFor's rules, so it never goes past the points of its level. Unlike ApplyCoaTalents it never
// lowers or removes an entry, and leaves a free-choice group alone once the owner picked one of its entries.
// Returns the number of ranks raised.
uint32 SpendCoaTalentPoints(Player* bot);

// Spends an alt bot's free talent points when AiPlayerbot.AutoPickTalentsAltBots is on: SpendCoaTalentPoints
// for a CoA class, the incremental talent template (no reset) for the others. Returns true when it spent any.
bool SpendAltBotTalentPoints(Player* bot);

// Brings the nearest free random bot able to fill `role` into the master's group: raised to
// the master's level, given a specialization of that role, teleported next to the master.
// Returns false with the reason in `message` when no bot can be recruited.
// Name of one of the 21 CoA classes, or nullptr.
char const* CoaClassName(uint8 classId);

// A CoA class from its name as typed in a command: case, spaces and apostrophes do not matter, and a
// beginning shared by no other class is enough ("sun" is Sun Cleric). 0 when none, or several, match.
uint8 FindCoaClass(std::string const& name);

// Recruits a free random bot for the role, of that class when classId is not 0.
bool RecruitCoaBot(Player* master, CoaRole role, std::string& message, uint8 classId = 0);

// Turns the master's group into a raid of `size` (25 when 0, 6 to 40) filled with recruited bots:
// `tanks` tanks and `heals` healers (from the size when 0: 3 and 6 for 25), the rest damage dealers,
// the classes taken in turn. Members already there count. The outcome is told in `message`.
bool RecruitCoaRaid(Player* master, uint32 size, uint32 tanks, uint32 heals, std::string& message);

// The free random bot best placed to play `role` for `master` (same faction, on its map, already of
// the role, nearest level), of that class when classId is not 0, none of those in `skip`. chosenFits
// tells whether it already holds a specialization of the role.
Player* FindCoaRecruit(Player* master, CoaRole role, uint8 classId, std::set<ObjectGuid> const& skip, bool& chosenFits);

// Makes the chosen bot ready to play `role` beside `master`: rebuilt at the master's level when more
// than levelTolerance levels away, given a specialization of the role and its gear, talents spent.
bool PrepareCoaRecruit(Player* master, Player* chosen, CoaRole role, bool chosenFits, uint32 levelTolerance,
                       std::string& message);

// "lfg bot heal" and the like, said by a player in a listened chat channel: free bots whisper offers.
void CoaLfgHeard(Player* player, std::string const& message, Channel* channel);

// Whether `bot` was offered to `inviter` by "lfg bot" and the offer still holds; the offer is used up.
bool CoaLfgTakeOffer(Player* bot, Player* inviter);
// The bot is offered by "lfg bot" to a player other than this one (any player when null): it keeps out of
// any other group or queue until the offer is taken or runs out.
bool CoaLfgOfferedToOther(Player* bot, Player* player);

// Takes a bot out of the battleground and Dungeon Finder queues it joined by itself, so its new group can
// queue for anything ("You cannot queue for a dungeon while using battlegrounds or arenas", PTR 28/09).
void CoaLeaveBattlegroundQueues(Player* bot);

// A random bot joining `master` drops its instance locks, but the one `master` stands in: a lock from an
// earlier run sent it into another Molten Core than its master's, and summoning never brought it over
// (jealous-sound/azerothcore-wotlk-coa#5557). Player characters and altbots keep theirs.
void CoaDropForeignInstanceBinds(Player* bot, Player* master);

// Whether `bot` stands on `master`'s map but in another copy of it: a summon must then change instance.
bool CoaInOtherInstance(Player* bot, Player* master);

void AddSC_coa_lfg();

// AiPlayerbot.Coa.AncestralCombatFix: a Barbarian that knows the passive Ancestral Combat (801782) without
// its aura gets it at the start of a fight. The spell asks for form 28 (Stances 0x8000000), which no
// Barbarian takes, so the core lays the aura only now and then: it was up in 43 % of the boss bench
// fights of 02/10, which then did 960 damage a second instead of 570. To remove once the core clears
// those Stances, as it does for other passives (AscensionGuardianCompletion.cpp).
void CoaRestoreAncestralCombat(Player* bot);

// The group fight log (AiPlayerbot.CoaGroupTelemetry) follows a group of bots only, as it follows a
// group with a real player, while `follow` is true. For test tools (mod-coa-diag); inert otherwise.
void CoaTelemetryFollowGroup(Group* group, bool follow);

#endif
