/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaTankForm.h"

#include "Player.h"
#include "PlayerbotAI.h"
#include "SharedDefines.h"
#include "UnitDefines.h"

bool CoaTankForm::IsBearTank(PlayerbotAI* botAI)
{
    Player* bot = botAI ? botAI->GetBot() : nullptr;
    if (!bot || bot->getClass() != CLASS_DRUID)
        return false;

    // A cat, balance or restoration druid is left alone: a bear form would break its rotation.
    if (!botAI->HasStrategy("bear", BOT_STATE_COMBAT))
        return false;

    return botAI->HasSpell("dire bear form") || botAI->HasSpell("bear form");
}

bool CoaTankForm::EnsureBearForm(PlayerbotAI* botAI)
{
    if (!IsBearTank(botAI))
        return false;

    Player* bot = botAI->GetBot();
    if (!bot->IsAlive() || bot->IsBeingTeleported())
        return false;

    ShapeshiftForm const form = bot->GetShapeshiftForm();
    if (form == FORM_BEAR || form == FORM_DIREBEAR)
        return false;

    char const* spell = botAI->HasSpell("dire bear form") ? "dire bear form" : "bear form";

    // From another form (travel off a mount, cat left over), drop it first. Removing a form is synchronous, so the
    // cast below still goes out on this tick.
    if (form != FORM_NONE)
        botAI->RemoveShapeshift();

    // CanCastSpell first: the usual blocked cases (cooldown, mana, silence) cost a test, not a failed cast.
    return botAI->CanCastSpell(spell, bot) && botAI->CastSpell(spell, bot);
}
