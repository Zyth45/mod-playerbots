/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "AllSpellScript.h"
#include "MCHelpers.h"
#include "Map.h"
#include "Playerbots.h"

using namespace MoltenCoreHelpers;

// CoA Conjure Flame Orb: a bot's engine does not run while it is casting, so the bots near Gehennas
// are asked from here to drop a cast under way and get out before the orb appears on him.
class GehennasConjureFlameOrbSpellListenerScript : public AllSpellScript
{
public:
    GehennasConjureFlameOrbSpellListenerScript() : AllSpellScript("GehennasConjureFlameOrbSpellListenerScript") {}

    void OnSpellPrepare(Spell* /*spell*/, Unit* caster, SpellInfo const* spellInfo) override
    {
        if (spellInfo->Id != SPELL_CONJURE_FLAME_ORB_COA || !caster)
            return;

        caster->GetMap()->DoForAllPlayers([caster](Player* player)
        {
            if (!player->IsAlive() || player->GetDistance2d(caster) >= FLAME_ORB_CAST_CLEAR_DISTANCE)
                return;

            PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);
            if (botAI && botAI->HasStrategy("moltencore", BOT_STATE_COMBAT))
                botAI->RequestSpellInterrupt();
        });
    }
};

void AddSC_MoltenCoreBotScripts() { new GehennasConjureFlameOrbSpellListenerScript(); }
