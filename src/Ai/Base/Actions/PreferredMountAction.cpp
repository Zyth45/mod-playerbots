/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "PreferredMountAction.h"

#include <algorithm>

#include "ChatHelper.h"
#include "CheckMountStateAction.h"
#include "Event.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"

namespace
{
enum MountKind : int8
{
    MOUNT_NONE = -1,
    MOUNT_GROUND = 0,
    MOUNT_FLYING = 1,
    MOUNT_BOTH = 2
};

// A mount carries the mounted aura; a flying one raises the mounted flight speed, and one that also raises the
// mounted ground speed fits both kinds (scaling mounts). Winged Steed of the Ebon Blade only flies, as in
// CollectMountData.
MountKind KindOf(SpellInfo const* info)
{
    bool mounted = false, flying = false, ground = false;
    for (SpellEffectInfo const& effect : info->Effects)
    {
        if (effect.ApplyAuraName == SPELL_AURA_MOUNTED)
            mounted = true;
        else if (effect.ApplyAuraName == SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED)
            flying = true;
        else if (effect.ApplyAuraName == SPELL_AURA_MOD_INCREASE_MOUNTED_SPEED)
            ground = true;
    }
    if (!mounted)
        return MOUNT_NONE;
    if (info->Id == 54729)
        return MOUNT_FLYING;
    if (flying)
        return ground ? MOUNT_BOTH : MOUNT_FLYING;
    return MOUNT_GROUND;
}

char const* KindName(MountKind kind)
{
    return kind == MOUNT_BOTH ? "both" : kind == MOUNT_FLYING ? "flying" : "ground";
}

bool Known(Player* bot, uint32 spellId)
{
    PlayerSpellMap const& spells = bot->GetSpellMap();
    auto const found = spells.find(spellId);
    return found != spells.end() && found->second->State != PLAYERSPELL_REMOVED && found->second->Active;
}
}

bool PreferredMountAction::Execute(Event event)
{
    std::string param = event.getParam();
    param.erase(0, param.find_first_not_of(' '));
    param.erase(param.find_last_not_of(' ') + 1);
    uint32 const guid = bot->GetGUID().GetCounter();

    if (param.empty() || param == "?")
    {
        std::vector<uint32> const ground = CheckMountStateAction::PreferredMounts(guid, MOUNT_GROUND);
        std::vector<uint32> const flying = CheckMountStateAction::PreferredMounts(guid, MOUNT_FLYING);
        std::vector<std::pair<std::string, uint32>> mounts;
        for (auto const& [spellId, spell] : bot->GetSpellMap())
        {
            if (spell->State == PLAYERSPELL_REMOVED || !spell->Active)
                continue;
            SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
            if (info && !info->IsPassive() && KindOf(info) != MOUNT_NONE)
                mounts.emplace_back(info->SpellName[0] ? info->SpellName[0] : "", spellId);
        }
        std::sort(mounts.begin(), mounts.end());

        for (auto const& [name, spellId] : mounts)
        {
            SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
            bool const preferred = std::find(ground.begin(), ground.end(), spellId) != ground.end() ||
                                   std::find(flying.begin(), flying.end(), spellId) != flying.end();
            std::ostringstream out;
            out << "Mount: " << chat->FormatSpell(info) << " " << KindName(KindOf(info));
            if (preferred)
                out << " (preferred)";
            botAI->TellMaster(out.str());
        }
        botAI->TellMaster("Mounts: " + std::to_string(mounts.size()));
        return true;
    }

    if (param.rfind("clear", 0) == 0)
    {
        size_t const rest = param.find_first_not_of(' ', 5);
        std::string const which = rest == std::string::npos ? "" : param.substr(rest);
        int8 const type = which == "ground" ? MOUNT_GROUND : which == "flying" ? MOUNT_FLYING : -1;
        CheckMountStateAction::ClearPreferredMount(guid, type);
        botAI->TellMaster("Preferred mount cleared");
        return true;
    }

    uint32 spellId = chat->parseSpell(param);
    if (!spellId)
        spellId = uint32(atoi(param.c_str()));
    SpellInfo const* info = spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
    if (!info)
    {
        botAI->TellMaster("Unknown spell: " + param);
        return false;
    }

    MountKind const kind = KindOf(info);
    if (kind == MOUNT_NONE)
    {
        botAI->TellMaster("Not a mount: " + chat->FormatSpell(info));
        return false;
    }
    if (!Known(bot, spellId))
    {
        botAI->TellMaster("Mount not known: " + chat->FormatSpell(info));
        return false;
    }

    if (kind != MOUNT_FLYING)
        CheckMountStateAction::SetPreferredMount(guid, MOUNT_GROUND, spellId);
    if (kind != MOUNT_GROUND)
        CheckMountStateAction::SetPreferredMount(guid, MOUNT_FLYING, spellId);
    botAI->TellMaster("Preferred mount set: " + chat->FormatSpell(info));
    return true;
}
