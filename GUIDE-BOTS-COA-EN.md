# CoA bots guide: commands and settings

For a Conquest of Azeroth server with the [mod-playerbots `coa` branch](https://github.com/Zyth45/mod-playerbots/tree/coa).
Every command below is typed **in the game chat**.

## In two minutes

1. **Recruit a bot** for the role you need; it joins your group and follows you:
   ```
   .playerbots coa tank
   .playerbots coa heal
   .playerbots coa dps
   ```
2. **Talk to it**: click its name in the group, or `/whisper BotName <command>`.
   ```
   /w BotName co ?
   ```
3. **Send it away**:
   ```
   .playerbots bot remove BotName
   ```

## Talking to a bot

The bot has to be **in your group**, otherwise it answers only one or two commands.
A trailing `?` **shows** instead of changing: `co ?` lists, `co +thing` adds, `co -thing` removes.

| Command | What it does |
|---|---|
| `co ?` | its combat strategies: its position (`close` / `ranged`), our `coa`, and its rotation `custom::class-spec` |
| `nc ?` | its strategies out of combat |
| `talents` | its CoA specialization |
| `talents spec list` | every spec of its class with its role, and a `>` in front of its own |
| `talents spec <name>` | switch its specialization (`tank`, `heal`, `dps` and `random` work too) |
| `spells` | its spells |
| `stats` | health, mana, gold, bags |
| `equip` / `autogear` / `upgrade` | its gear |
| `follow`, `stay`, `flee`, `attack` | its movement |
| `formation`, `position`, `rti` | its place in the group and its marked target |
| `home`, `teleport`, `taxi`, `repair`, `bank`, `trainer` | its travels and services |
| `quests`, `do quest`, `share` | its quests |
| `help` | the full list |

## Game master commands

`.playerbots bot` manages your bots, `.playerbots rndbot` manages the server's random bots.

| Command | What it does |
|---|---|
| `.playerbots coa tank\|heal\|dps` | recruits a CoA bot playing that role, preferably close to your level, rebuilds it exactly at your level (gear and talents included, up as well as down) and teleports it to you |
| `.playerbots bot add <name>` | takes control of a given bot |
| `.playerbots bot addclass <class>` | creates a bot of a given class |
| `.playerbots bot remove <name>` | sends it away |
| `.playerbots bot list` | your bots |
| `.playerbots bot self` | drives **your own character** like a bot |
| `.playerbots rndbot stats` | the state of the random bots |
| `.playerbots rndbot teleport` | **sends them all to a zone fitting their level**, without waiting for the automatic teleport |
| `.playerbots rndbot grind` | sends them hunting |
| `.playerbots rndbot init` | redoes their level, gear and talents |
| `.playerbots rndbot levelup` | gives them a level |
| `.playerbots rndbot revive` | resurrects the dead |
| `.playerbots rndbot refresh` | heals them and makes them good as new |
| `.playerbots rndbot reload` | re-reads `playerbots.conf` without a restart |

The `.playerbots rndbot` commands apply to every bot online. To target just one:
`.playerbots rndbot teleport BotName`.

## How the world fills itself

- Bots are spread over **nine level brackets** (1-3, 4-7, 8-12, 13-17, 18-23, 24-30, 31-40, 41-50, 51-60),
  rebalanced every 5 minutes: about two thirds are below level 30.
- **Brackets where real players play draw more bots**: with a level 15 character, you meet more bots around 15.
- **New level 1 bots keep appearing in the starting valleys** (Northshire, Coldridge, Shadowglen, Ammen Vale,
  Valley of Trials, Deathknell, Red Cloud Mesa, Sunstrider), shared evenly between each faction's valleys. As they
  level up and leave, others take their place.

## Useful settings (`playerbots.conf`)

| Setting | Effect |
|---|---|
| `AiPlayerbot.MinRandomBots` / `MaxRandomBots` | how many bots play at the same time (about 10 GB of RAM for 1000) |
| `AiPlayerbot.LevelBrackets.*` | the brackets above; `Dynamic.RealPlayerWeight` sets how strongly players draw bots |
| `AiPlayerbot.LevelBrackets.FreshStart` / `FreshStartSpread` | level 1 newcomers in the starting valleys / shared between the valleys |
| `AiPlayerbot.RandomBotMinLevel` / `RandomBotMaxLevel` | the level range rolled on a bot's first login |
| `AiPlayerbot.BotActiveAlone` | share of bots active when no player is near them (60 recommended; the default 10 leaves almost everything still) |
| `AiPlayerbot.MinRandomBotTeleportInterval` / `Max...` | time between two automatic moves, in seconds (3600 to 18000 by default) |
| `AiPlayerbot.GroupInvitationPermission` | at 2, every bot accepts invitations |
| `AiPlayerbot.BotTextLocale` | bot chat language: 0 for English, 2 for French |
| `AiPlayerbot.CoaHealerManaReserve` / `CoaCasterManaReserve` | share of mana kept for heals instead of spent on damage, in percent: 35 for healers, 15 for other bots that know a heal. Below it, the bot attacks with what costs nothing and its weapon. A bot without any heal is never held back; 0 disables it |
| `AiPlayerbot.ZoneChannelId` | number of the zone channel, as the client gives it. **3 on CoA** (the "Zone - …" channel), 1 on a stock client. Wrong number = bots talk to nobody |
| `AiPlayerbot.BroadcastWorldChannelName` | exact name of the realm-wide channel. `"Ascension"` on CoA, `"World"` elsewhere |
| `AiPlayerbot.BroadcastToWorldGlobalChance` | share of messages sent to the realm-wide channel, out of 30,000. **0 = bots do not even join it** |
| `AiPlayerbot.BroadcastToGeneralGlobalChance` | share of messages sent to the zone channel, out of 30,000 |
| `Who.ShowBots` (in worldserver.conf) | at 1, the default, bots appear in the /who list like players; at 0 the list is left to the real players. `.reload config` applies it without a restart |
| `AiPlayerbot.CoaClassesOnly` | at 1 (the default), random bots are created in CoA classes only, whatever `CharacterCreating.Disabled.ClassMask` says in worldserver.conf; at 0, every class the core allows |
| `AiPlayerbot.CoaSpecRotations` | at 1, bots follow their specialization's authored rotation on top of the automatic spell choice (0 by default) |
| `AiPlayerbot.DeleteRandomBotAccounts` | at 1, deletes every bot on the next start, then creates new ones. **The server then stops by itself**: set it back to 0 and restart |
| `Appender.CoaBots=2,4,1,CoaBots.log,a` and `Logger.playerbots.coa=4,CoaBots` (in `worldserver.conf`) | every 10 minutes, writes which CoA spells the bots cast and why some fail |

## Pitfalls

- **`co` alone does not answer**, you need `co ?`. Same for `nc`.
- **Out of a group**, a bot ignores most commands.
- **An inactive bot does not move**: out of a group and far from players, only part of the bots are active (`BotActiveAlone`).
- **A bot can change level at once**: that is the bracket rebalancing. It is then sent to a zone of its new level.
- **There are more bots in total than bots online**: the module keeps a reserve and rotates the characters.

## When something goes wrong

- **A bot does not cast its spells**: check its spec with `talents`, its strategies with `co ?`, and turn on the `CoaBots.log` journal above.
- **The realm shows "offline"** while the server runs: its flag was left at 2 in `acore_auth.realmlist`; set it to 0.
- **A crash**: the report is in `Core\Crashes`; keep the `.txt` and the `.dmp`, they point at the cause.

## Credits

- [jealous-sound](https://github.com/jealous-sound/azerothcore-wotlk-coa) for the Conquest of Azeroth server
- [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots) for the bots
- [ascensionsidekick.com](https://ascensionsidekick.com) for the specialization roles and talent builds
- [steviecraycray](https://github.com/steviecraycray) for the authored per-specialization rotations, the gear weights
  and several fixes
