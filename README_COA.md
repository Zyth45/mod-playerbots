# SquidBots: mod-playerbots for Conquest of Azeroth

SquidBots is the bots module of Conquest of Azeroth servers. It runs on the official
[Conquest of Azeroth core](https://github.com/jealous-sound/azerothcore-wotlk-coa) and adds bots that play the
**21 Conquest of Azeroth classes** (class ids 12 to 32):

- random bots of every CoA class take a specialization (tank, healer or damage) and spend their talents level by level;
- CoA spells are classified from `Spell.dbc` (heals, HoTs, taunts, AoE, buffs, defensives, dispels, interrupts), so
  tanks taunt, healers heal and dispel, and everyone interrupts;
- each specialization follows an authored rotation on top of the automatic spell choice (`AiPlayerbot.CoaSpecRotations`);
- gear is weighted with the stats of the bot's specialization;
- players find bots without GM commands: `lfg bot tank|heal|dps` in a channel, or `.playerbots coa tank|heal|dps`.

**News, new versions and support: the [SquidBots Discord](https://discord.gg/DTJceaG9Ky).**

## Current version: 3.1.5

The release notes of each 3.x version are in [`changelog.d/`](changelog.d), one file per change; the history from 1.1
to 2.9.4 is in [CHANGELOG_COA.md](CHANGELOG_COA.md). [`squidbots-release.json`](squidbots-release.json) lists, for
each release, every SQL file with its sha256, the new options and the components released together.

3.1.5 adds three options contributed by stefanw1337, all **on by default** (set one to 0 in `playerbots.conf` to turn it off) and not verified in game yet:
`AiPlayerbot.FlyOnlyOutOfSight`, `AiPlayerbot.TeleportOnlyOutOfSight` and `AiPlayerbot.StandOffObjects`.

## Install

The **SquidBots Server** launcher installs and updates the whole set (server, module, addon, dashboard), with a backup
before every update. Its update files are published on
[Zyth45/squidbots-updates](https://github.com/Zyth45/squidbots-updates).

A full SquidBots is released as one set; `squidbots-release.json` (`components`) names the tags that go together:

| Part | Where | Required |
|---|---|---|
| SquidBots module (this repository) | server, `modules/mod-playerbots` | yes |
| [Dungeon Clear](https://github.com/Zyth45/mod-dungeon-clear) | server, `modules/mod-dungeon-clear` | no: without it the bots play, but bot groups do not clear dungeons and raids with a player |
| [SquidBots addon](https://github.com/Zyth45/squidbots-addon) | the players' client | no |
| [SquidBots dashboard](https://github.com/Zyth45/squidbots-dashboard) | server tool | no |

**Found a crash?** Open an issue with the newest `.txt` file from the `Crashes` folder next to `worldserver.exe`, and
tell what the bots were doing (class, level, dungeon or open world) if you know.

## Building it into your own server

Put the module in `modules/mod-playerbots` of the core, pick a release tag (`vX.Y.Z`) and build as usual. Everything
else (MSVC library split, optional core headers, databases and SQL, added races, logs) is in
**[docs/INTEGRATING-THIRD-PARTY-SERVER.md](docs/INTEGRATING-THIRD-PARTY-SERVER.md)**.

> [!WARNING]
> The build writes `playerbots.conf.dist`, never `playerbots.conf`, and `playerbots.conf` is the file the server
> reads. **An old `playerbots.conf` keeps its old values in silence.** The launcher merges new options into it on
> update; by hand, compare it with the reference file.

## Recommended settings

The values of `playerbots.conf.dist` are already the ones a CoA realm wants. The ones people change most:

| Setting | File | Default | What it does |
|---|---|---|---|
| `AiPlayerbot.MinRandomBots` / `MaxRandomBots` | playerbots.conf | 500 / 500 | how many random bots are online; more bots need more memory and more map threads |
| `MapUpdate.Threads` | worldserver.conf | 1 | raise it (for example to half your CPU threads) for hundreds of bots |
| `AiPlayerbot.CoaClassesOnly` | playerbots.conf | 1 | random bots only take the CoA classes |
| `AiPlayerbot.RandomBotMaxLevel` | playerbots.conf | 60 | random bots spread over levels 1 to 60 |
| `AiPlayerbot.LevelBrackets.Enabled` | playerbots.conf | 1 | bots are rebalanced across level brackets |
| `AiPlayerbot.LevelBrackets.FreshStart` | playerbots.conf | 1 | a bot moved back to level 1 starts again at its race's starting point, like a new player |
| `AiPlayerbot.BotActiveAlone` | playerbots.conf | 60 | share of bots that stay active away from players |
| `AiPlayerbot.GroupInvitationPermission` | playerbots.conf | 2 | every bot accepts group invites |
| `AiPlayerbot.BotTextLocale` | playerbots.conf | -1 | bot chat language: -1 the client's, 0 English, 2 French, 3 German, 6 Spanish, 8 Russian |
| `AiPlayerbot.CoaExcludedSpecializations` | playerbots.conf | empty | specializations bots never take, by id; empty means every specialization is played |
| `AiPlayerbot.CoaLfgBots` / `CoaLfgChannels` | playerbots.conf | 1 / `Zone,Newcomers,World` | `lfg bot ...` typed in these channels gets offers from free bots |
| `AiPlayerbot.CoaHealerManaReserve` / `CoaCasterManaReserve` | playerbots.conf | 35 / 15 | share of its mana a bot keeps for healing instead of damage |
| `AiPlayerbot.ZoneChannelId` | playerbots.conf | 3 | number of the per-zone channel on CoA ("Zone - <place>") |
| `AiPlayerbot.BroadcastWorldChannelName` | playerbots.conf | `Ascension` | the realm-wide channel; `BroadcastToWorldGlobalChance = 0` keeps bots out of it |

Races: random bots take the ten base races; a server that adds races describes each one with
`AiPlayerbot.BotRace.<id>` (see the integration guide).

## Commands

Typed in the game chat. A bot has to be **in your group** to answer most whispers.

| Command | What it does |
|---|---|
| `lfg bot tank\|heal\|dps` | typed in a channel of `CoaLfgChannels`, by any player: free bots of those roles answer with an offer, accepted by inviting them |
| `.playerbots coa tank\|heal\|dps` | recruits a free CoA bot of that role into your group |
| `.playerbots bot add\|remove <name>` | takes control of a bot, or sends it away |
| `.playerbots rndbot reload` | re-reads `playerbots.conf` without a restart (GM) |
| `/w <bot> co ?` | its combat strategies (`co ?` lists, `co +name` adds, `co -name` removes) |
| `/w <bot> talents` | its specialization and talents |
| `/w <bot> help` | the full list |

## The addon

The **[SquidBots addon](https://github.com/Zyth45/squidbots-addon)** gives players a bar of orders for their bots and
a minimap button; `/sbl` shows or hides the bar. Everything it sends is a chat command the module already understands.

## Credits

- [jealous-sound](https://github.com/jealous-sound/azerothcore-wotlk-coa) for the Conquest of Azeroth server
- [AzerothCore](https://github.com/azerothcore/azerothcore-wotlk) and
  [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots)
- [ascensionsidekick.com](https://ascensionsidekick.com) for the CoA specialization roles and talent builds
- [steviecraycray](https://github.com/steviecraycray) for the authored per-specialization rotations, the gear
  weights and weapon shapes, and several fixes the nine base classes benefit from as well
- stefanw1337 for the out-of-sight options of 3.1.5

Same license as mod-playerbots (GPL-2.0). The combat guards in `src/Ai/Coa/Gardes/` (and `tests/dc294/`) come from
Dungeon Clear by Jared Wright and contributors and stay under AGPL-3.0-or-later: see `LICENSE-GARDES` and `AUTHORS.md`.
