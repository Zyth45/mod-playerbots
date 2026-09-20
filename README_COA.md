# mod-playerbots for Conquest of Azeroth

This `coa` branch of mod-playerbots adds bots for the **Conquest of Azeroth custom classes** of the
[jealous-sound CoA server](https://github.com/jealous-sound/azerothcore-wotlk-coa):

- random bots of every CoA class pick a specialization at level 10 (tank / healer / DPS) and spend their talents
  level by level (builds from [ascensionsidekick.com](https://ascensionsidekick.com));
- CoA spells are classified from `Spell.dbc` (heals, HoTs, taunts, AoE, buffs, defensives, dispels, interrupts),
  so tanks taunt, healers heal and dispel, and everyone interrupts;
- gear is weighted with the stats of the bot's CoA specialization;
- `.playerbots coa tank|heal|dps` recruits a bot of that role into your group.

## Current status

> [!NOTE]
> **Bots run over the whole 1-60 range.** On 19 September 2026 a test realm ran 1000 bots spread over every level,
> the level brackets moving them all night; its one crash, bots recursing over items crafted from each other, is
> fixed. Higher-level CoA spells have been cast by bots
> far less than low-level ones, so a crash is still possible there: please report it (below).

**Found a crash?** Open an issue with the newest `.txt` file from the `Crashes` folder next to `worldserver.exe`,
and tell what the bots were doing (class, level, dungeon or open world) if you know.

## Which versions go together

The module follows jealous-sound's `main`; older releases keep the tag they were built with.

| jealous-sound | This module |
|---|---|
| **`main`** from `b3717c1` (19 Sep 2026, repack main-20260919-b3717c137) | branch **`coa`** (same code as the CoA Bots v1.2 zip) |
| issue-batch-20260915 (`a8b28faac98d`) | tag `bots-issue-batch-20260915-v1.1`, with the core tag of the same name from [Zyth45/azerothcore-wotlk-coa](https://github.com/Zyth45/azerothcore-wotlk-coa) |

Since 17 September 2026 jealous-sound's `main` carries everything the bots need from the core: the mod-playerbots
hooks and the CoA specialization API ([#3069](https://github.com/jealous-sound/azerothcore-wotlk-coa/pull/3069))
and the fixes for the crashes bots trigger often (#3072, #3075, #3080, #3083, #3085). **No fork of the core is
needed any more**: the module goes into `modules/`, which the core's `.gitignore` leaves alone, so pulling `main`
never conflicts with it.

Optional: [#4154](https://github.com/jealous-sound/azerothcore-wotlk-coa/pull/4154) (until it is merged) makes the
challenges module much lighter with many bots online.

## Build

1. Get the core and this module:

   ```bash
   git clone https://github.com/jealous-sound/azerothcore-wotlk-coa.git
   git clone --branch coa https://github.com/Zyth45/mod-playerbots.git azerothcore-wotlk-coa/modules/mod-playerbots
   ```

   Already building the core? Only the second line, then re-run CMake so it picks the module up.

2. Build and install the server as usual:
   [AzerothCore installation guide](https://www.azerothcore.org/wiki/installation) and
   [mod-playerbots installation guide](https://github.com/mod-playerbots/mod-playerbots/wiki/Installation-Guide).
   Only `mod-ascension-compat` (already in the core) and `mod-playerbots` are needed.
   On Windows, copy `libmysql.dll`, `libcrypto-3-x64.dll`, `libssl-3-x64.dll` and `legacy.dll` next to
   `worldserver.exe`.

3. Game data: the CoA `dbc`, `maps`, `vmaps` and `mmaps` cannot be extracted from the client with the stock
   extractors. Use the `Data` folder of the jealous-sound CoA repack and point `DataDir` to it.

4. Databases: import the world with `apps/coa-world/world_data.py bootstrap` (see `apps/coa-world/README.md`),
   create an empty `acore_playerbots` database for the `acore` user, then start worldserver once
   (`Updates.AutoSetup = 1`) to create the other tables.

5. Copy `playerbots.conf.dist` to `playerbots.conf` and set `PlayerbotsDatabaseInfo`.

## Recommended settings

| Setting | File | Why |
|---|---|---|
| `CharacterCreating.Disabled.ClassMask = 2047` | worldserver.conf | stops **players** rolling the nine WotLK classes; the bots no longer need it, `AiPlayerbot.CoaClassesOnly` keeps them on CoA classes on its own |
| `MapUpdate.Threads = 8` (half your CPU threads) | worldserver.conf | hundreds of bots need several map threads |
| `AiPlayerbot.MinRandomBots = 200` / `MaxRandomBots = 200` | playerbots.conf | about 3.5 GB of server memory for 200 bots, 10 GB for 1000 |
| `AiPlayerbot.RandomBotMaxLevel = 60` | playerbots.conf | the CoA default: random bots spread over levels 1-60 (80 upstream); set 1 instead to have every bot start at level 1 and level up while playing, which also leaves the level brackets below with nothing to balance |
| `AiPlayerbot.LevelBrackets.Enabled = 1` with `Dynamic.UseDynamicDistribution = 1` | playerbots.conf | the CoA default: bots are rebalanced across 9 level brackets every 5 minutes, narrow up to 30 (1-3 on its own), so about two thirds of them are in 1-30; brackets holding a real player draw more bots (`Dynamic.RealPlayerWeight = 3.0`) |
| `AiPlayerbot.LevelBrackets.FreshStart = 1` | playerbots.conf | the CoA default: a bot moved into the 1-3 bracket comes back at level 1 at its race's starting point, like a new player, and a bot moved to any other level is sent somewhere fitting that level |
| `AiPlayerbot.BotActiveAlone = 60` | playerbots.conf | more bots stay active away from players |
| `AiPlayerbot.GroupInvitationPermission = 2` | playerbots.conf | every bot accepts group invites |
| `AiPlayerbot.BotTextLocale = 2` | playerbots.conf | bot chat in French (-1 client locale, 0 English, 3 German, 6 Spanish, 8 Russian) |
| `AiPlayerbot.CoaSpecRotations = 1` | playerbots.conf | bots follow the authored rotation of their specialization on top of the automatic spell choice (default: 0) |
| `AiPlayerbot.CoaHealerManaReserve = 35` / `CoaCasterManaReserve = 15` | playerbots.conf | share of its mana a bot keeps for healing instead of spending it on damage: the larger share for healers, the smaller one for anything else that knows a heal; a bot with no heal is never held back, 0 disables it |
| `AiPlayerbot.ZoneChannelId = 3` | playerbots.conf | the per-zone channel is numbered 3 on CoA ("Zone - <place>"), 1 on a stock client; with the wrong number bots talk where nobody reads |
| `AiPlayerbot.BroadcastWorldChannelName = "Ascension"` | playerbots.conf | the realm-wide channel's name; set `BroadcastToWorldGlobalChance = 0` to keep bots out of it entirely |
| `Appender.CoaBots=2,4,1,CoaBots.log,a` and `Logger.playerbots.coa=4,CoaBots` | worldserver.conf | optional: every 10 minutes, which CoA spells bots cast and why casts fail |

Known issue: `mod-aoe-loot` crashes the CoA release at the first bot login (its module string is missing from the
release database). Leave it out or import its SQL.

## Commands

Everything below is typed in the game chat. A bot has to be **in your group** to answer most whispers, and a
trailing `?` shows instead of changes: `co ?` lists, `co +name` adds, `co -name` removes.

| Command | What it does |
|---|---|
| `.playerbots coa tank\|heal\|dps` | recruits the nearest free CoA bot of that role into your group, raised to your level |
| `.playerbots bot add\|remove <name>` | takes control of a bot, or sends it away |
| `.playerbots bot self` | drives your own character with the bot AI |
| `.playerbots rndbot teleport` | sends every random bot to a place that fits its level, instead of waiting for the automatic move |
| `.playerbots rndbot stats\|grind\|init\|levelup\|revive\|refresh` | state, send hunting, re-roll level/gear/talents, level up, revive, restore |
| `.playerbots rndbot reload` | re-reads `playerbots.conf` without a restart |
| `/w <bot> co ?` | its combat strategies: position (`close` / `ranged`), the CoA classifier, and its authored rotation |
| `/w <bot> talents` / `talents spec list` / `talents spec <name>` | its specialization, the list with roles, or switch (`tank`, `heal`, `dps`, `random` work too) |
| `/w <bot> stats\|spells\|equip\|autogear\|upgrade` | its state, spells and gear |
| `/w <bot> follow\|stay\|flee\|attack\|formation\|rti` | movement and group position |
| `/w <bot> help` | the full list |

Things that surprise people:

- `co` alone answers nothing, it needs `co ?`.
- Out of a group, a bot ignores most commands.
- Bots do not level from 1: each one is given a random level on its first login, with the gear and talents that go
  with it, and only moves to a zone of its level on the next automatic teleport (up to five hours).
- The bot pool is larger than the number online: the module rotates characters.

## Updating to a new jealous-sound release

The bot work is a few commits on top of the release in each repository. Cherry-pick them onto the new release
revision (`targetSourceRevision` in the release `UPDATE.json`), rebuild, and tag `bots-<release id>`.

## Credits

- [jealous-sound](https://github.com/jealous-sound/azerothcore-wotlk-coa) for the Conquest of Azeroth server
- [AzerothCore](https://github.com/azerothcore/azerothcore-wotlk) and
  [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots)
- [ascensionsidekick.com](https://ascensionsidekick.com) for the CoA specialization roles and talent builds
- [steviecraycray](https://github.com/steviecraycray) for the authored per-specialization rotations, the gear
  weights and weapon shapes, and several fixes the nine base classes benefit from as well

Same license as mod-playerbots (GPL-2.0).
