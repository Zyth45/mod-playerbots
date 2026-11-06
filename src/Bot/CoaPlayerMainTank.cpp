/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "CoaPlayerMainTank.h"

#include "GameTime.h"
#include "Group.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "ScriptMgr.h"
#include "Unit.h"

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace
{
    constexpr uint64 ReleaseMs = 15000;
    constexpr uint8 NoIcon = 0xFF;
    constexpr uint8 IconOrder[] = {5, 3, 2, 6, 0, 1};

    struct PinnedTarget
    {
        ObjectGuid target;
        uint8 icon = NoIcon;
    };

    struct ReleasedTarget
    {
        ObjectGuid target;
        uint64 untilMs = 0;
    };

    std::mutex Lock;
    std::unordered_set<ObjectGuid> Flagged;
    std::atomic<uint32> FlaggedCount{0};
    std::unordered_map<ObjectGuid, PinnedTarget> Pins;
    std::unordered_map<ObjectGuid, ReleasedTarget> Releases;

    uint64 NowMs() { return GameTime::GetGameTimeMS().count(); }
}

bool CoaPlayerMainTank::IsSet(Player const* player)
{
    if (!FlaggedCount || !player)
        return false;
    std::lock_guard<std::mutex> guard(Lock);
    return Flagged.contains(player->GetGUID());
}

Player* CoaPlayerMainTank::FlaggedInGroup(Group const* group, Player const* reference)
{
    if (!FlaggedCount || !group || !reference)
        return nullptr;
    for (GroupReference* ref = const_cast<Group*>(group)->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (member && member->IsInWorld() && member->GetMap() == reference->GetMap() && IsSet(member))
            return member;
    }
    return nullptr;
}

void CoaPlayerMainTank::Set(Player* player, bool on)
{
    if (!player || GET_PLAYERBOT_AI(player))
        return;
    std::lock_guard<std::mutex> guard(Lock);
    if (on)
        Flagged.insert(player->GetGUID());
    else
        Flagged.erase(player->GetGUID());
    FlaggedCount = uint32(Flagged.size());
}

bool CoaPlayerMainTank::HeldByPlayer(Player const* bot, Unit const* enemy)
{
    return Hold(bot, enemy) == CoaPlayerHold::Holds;
}

CoaPlayerHold CoaPlayerMainTank::Hold(Player const* bot, Unit const* enemy)
{
    if (!FlaggedCount || !bot || !enemy)
        return CoaPlayerHold::None;
    Unit const* victim = enemy->GetVictim();
    Player const* player = victim ? victim->ToPlayer() : nullptr;
    if (!player || player == bot || !player->IsAlive() || !IsSet(player) || !player->IsInSameRaidWith(bot))
        return CoaPlayerHold::None;
    return player->GetHealthPct() > float(sPlayerbotAIConfig.playerMainTankRescueHealth) ? CoaPlayerHold::Holds
                                                                                         : CoaPlayerHold::Rescue;
}

void CoaPlayerMainTank::Pin(Player* bot, ObjectGuid target, uint8 icon)
{
    std::lock_guard<std::mutex> guard(Lock);
    Pins[bot->GetGUID()] = PinnedTarget{target, icon};
    Releases.erase(bot->GetGUID());
}

ObjectGuid CoaPlayerMainTank::Pinned(Player const* bot, uint8* icon)
{
    if (icon)
        *icon = NoIcon;
    if (!bot)
        return ObjectGuid::Empty;
    std::lock_guard<std::mutex> guard(Lock);
    auto const itr = Pins.find(bot->GetGUID());
    if (itr == Pins.end())
        return ObjectGuid::Empty;
    if (icon)
        *icon = itr->second.icon;
    return itr->second.target;
}

void CoaPlayerMainTank::Unpin(Player* bot)
{
    uint8 icon = NoIcon;
    ObjectGuid const target = Pinned(bot, &icon);
    {
        std::lock_guard<std::mutex> guard(Lock);
        Pins.erase(bot->GetGUID());
    }
    // The icon goes with the order, unless the player moved it to another target since.
    if (Group* group = bot->GetGroup())
        if (icon != NoIcon && target && group->GetTargetIcon(icon) == target)
            group->SetTargetIcon(icon, bot->GetGUID(), ObjectGuid::Empty);
}

void CoaPlayerMainTank::Release(Player* bot, ObjectGuid target)
{
    std::lock_guard<std::mutex> guard(Lock);
    Releases[bot->GetGUID()] = ReleasedTarget{target, NowMs() + ReleaseMs};
}

bool CoaPlayerMainTank::Released(Player const* bot, ObjectGuid target)
{
    if (!bot || !target)
        return false;
    std::lock_guard<std::mutex> guard(Lock);
    auto const itr = Releases.find(bot->GetGUID());
    return itr != Releases.end() && itr->second.target == target && NowMs() < itr->second.untilMs;
}

uint8 CoaPlayerMainTank::NextFreeIcon(Group const* group)
{
    if (!group)
        return NoIcon;
    for (uint8 icon : IconOrder)
        if (!group->GetTargetIcon(icon))
            return icon;
    return IconOrder[0];
}

void CoaPlayerMainTank::Forget(ObjectGuid guid)
{
    std::lock_guard<std::mutex> guard(Lock);
    Flagged.erase(guid);
    FlaggedCount = uint32(Flagged.size());
    Pins.erase(guid);
    Releases.erase(guid);
}

class CoaPlayerMainTankPlayerScript : public PlayerScript
{
public:
    CoaPlayerMainTankPlayerScript()
        : PlayerScript("CoaPlayerMainTankPlayerScript", {PLAYERHOOK_ON_LOGOUT, PLAYERHOOK_ON_PLAYER_JUST_DIED}) {}

    void OnPlayerLogout(Player* player) override
    {
        if (player)
            CoaPlayerMainTank::Forget(player->GetGUID());
    }

    // A bot tank that dies (a wipe) drops its order and its raid icon: the target it was given resets with the
    // fight, and the next pull is the player's to give again (06/11 review, point 8).
    void OnPlayerJustDied(Player* player) override
    {
        if (player && GET_PLAYERBOT_AI(player) && CoaPlayerMainTank::Pinned(player))
            CoaPlayerMainTank::Unpin(player);
    }
};

void AddSC_coa_player_main_tank()
{
    new CoaPlayerMainTankPlayerScript();
}
