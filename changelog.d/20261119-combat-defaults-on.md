### Combat fixes on by default
Ten combat settings that were off by default are now on for every realm, also on servers that update. A value you set yourself in playerbots.conf is kept.
- `AiPlayerbot.CoaCombat.StuckCombat.Enable`, `CombatPurge.Enable`, `SunkenTemple.Enable`, `CallOfTheGrave.Enable`, `PlayerLed.Enable`, `GearScore.Enable`: 1.
- The raid fixes `AiPlayerbot.CoaCombat.BlackwingLair.Enable`, `ZulGurub.Enable` and `AhnQiraj.Enable`: 1.
- `AiPlayerbot.CoaCombat.LootNeedMinUpgradePct`: 10. A bot keeps its Need only on an item at least 10 % better than the one it wears; a smaller upgrade is rolled Greed.
