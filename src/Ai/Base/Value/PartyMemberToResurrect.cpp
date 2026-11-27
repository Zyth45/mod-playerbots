/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "PartyMemberToResurrect.h"
#include "Playerbots.h"

class IsTargetOfResurrectSpell : public SpellEntryPredicate
{
public:
    bool Check(SpellInfo const* spellInfo) override
    {
        for (uint8 i = 0; i < 3; ++i)
        {
            if (spellInfo->Effects[i].Effect == SPELL_EFFECT_RESURRECT ||
                spellInfo->Effects[i].Effect == SPELL_EFFECT_RESURRECT_NEW ||
                spellInfo->Effects[i].Effect == SPELL_EFFECT_SELF_RESURRECT)
                return true;
        }

        return false;
    }
};

class FindDeadPlayer : public FindPlayerPredicate
{
public:
    FindDeadPlayer(PartyMemberValue* value, Player* bot) : value(value), bot(bot) {}

    bool Check(Unit* unit) override
    {
        Player* player = unit->ToPlayer();
        // A corpse behind a door or a wall can never be reached by the cast: the healer used to
        // spam the resurrection at it.
        return player && !player->isResurrectRequested() && player->getDeathState() == DeathState::Corpse &&
               bot->IsWithinLOSInMap(player) && !value->IsTargetOfSpellCast(player, predicate);
    }

private:
    PartyMemberValue* value;
    Player* bot;
    IsTargetOfResurrectSpell predicate;
};

Unit* PartyMemberToResurrect::Calculate()
{
    // CoA 3.2.2: never in a battleground or an arena. A battleground's dead come back at its spirit healer within half a
    // minute, so a healer stopping to cast a resurrection at a corpse left the fight for nothing; an arena's dead stay
    // dead. The bot fights on instead.
    if (bot->InBattleground() || bot->InArena())
        return nullptr;
    FindDeadPlayer finder(this, bot);
    return FindPartyMember(finder);
}
