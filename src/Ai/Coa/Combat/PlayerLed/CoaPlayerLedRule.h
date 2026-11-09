/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COA_PLAYER_LED_RULE_H
#define PLAYERBOTS_COA_PLAYER_LED_RULE_H

// "The player leads" (docs/JOUEUR-MENE-3.0.md): what the bots take from a real player who leads a group of bots in a
// dungeon or a raid, where Dungeon Clear holds the same roles in its autopilot. No game types, for the unit test.
namespace CoaPlayerLedRule
{
    enum class MainTankSource
    {
        None,
        PlayerBar,   // the player who opened the "Main Tank" bar ("tank main on")
        RaidFrame,   // the member flagged main tank in the raid frame
        FirstTank    // the first living tank of the group
    };

    // Who is the group's main tank for the raid strategies (PlayerbotAI::GetMainTankGuid). With
    // AiPlayerbot.CoaCombat.PlayerLed.Enable, the player's bar comes first, as for CoaGroupTank::Reference; the stock
    // order follows.
    inline MainTankSource ChooseMainTank(bool playerLedOn, bool barPlayerAlive, bool raidFrameTankAlive,
                                         bool anyTankAlive)
    {
        if (playerLedOn && barPlayerAlive)
            return MainTankSource::PlayerBar;
        if (raidFrameTankAlive)
            return MainTankSource::RaidFrame;
        if (anyTankAlive)
            return MainTankSource::FirstTank;
        return MainTankSource::None;
    }

    // Dungeon Clear's roster of bosses answers only inside its runs. Outside one (a player leads, or Dungeon Clear is
    // off), the boss stand-down falls back to the creature's own boss flags, with PlayerLed on.
    inline bool UseRosterProvider(bool providerSet, bool inDungeonClearRun, bool playerLedOn)
    {
        return providerSet && (inDungeonClearRun || !playerLedOn);
    }

    // The stuck-combat breaker inside a Dungeon Clear run: Dungeon Clear's hook decides, except when the reference
    // tank is a real player, where the hook always says no: the module's own key decides then.
    inline bool StuckCombatAllowed(bool inDungeonClearRun, bool referenceIsHuman, bool runHookAllows, bool keyOn)
    {
        if (!inDungeonClearRun || referenceIsHuman)
            return keyOn;
        return runHookAllows;
    }
}

#endif
