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
> **CoA Bots 2.4 (16 October 2026)**: delivered by the **SquidBots launcher**. Smarter dungeons (the tank walks its
> route, melee out of the boss's cone, damage dealers wait for the pull), buffs on a cooldown back on time, rotations
> from players' tips, bots leave a battleground queue that will never start, every module's settings in the right place
> (no more log flood), and a clean server stop. Built on jealous-sound's `main` of 15 October 2026 (`9dc61efc`).
>
> **CoA Bots 2.1 (12 October 2026)**: 1v1 skirmish and a ready pylon in arenas, smarter arena bots (kiting, pillars,
> focus), your alts managed from SquidBots Lite (bot sheet, mounts, talents), dungeon fixes from headless test runs,
> and a core fix for servers that slowed down over hours. Built on jealous-sound's `main` of 12 October 2026
> (`d5ff1cfa`).
>
> **CoA Bots 2.0 (11 October 2026)**: tanks that hold, dungeons played like players, battlegrounds and rated arenas.
> Built on jealous-sound's `main` of 11 October 2026 (`0a0a21d3`). Benched 24/7 on three realms before release.
>
> - **Tanking**: damage dealers, healers and pets never taunt; damage dealers stay under the tank's threat and only
>   use their AoE once the pack is on the tank; tanks keep all their threat auras. No hidden threat bonus
>   (`CoaTankThreatBonus = 0`).
> - **Dungeons and mythic+** (`CoaDungeonPriorities`): heals, crowd control, summons and about a hundred dungeon boss
>   casts are interrupted first; bosses are purged and soothed; adds and totems that heal or protect the boss die first.
> - **PvP**: bots fill battlegrounds, play High Risk and War Mode, and queue rated 2v2, 3v3 and 5v5 from the PvP
>   frame; they wear PvP gear (resilience, tiers by rating) there and PvE gear in dungeons (`CoaGearByContent`).
> - **Rotations**: all 70 specializations, 15 of them reworked for 2.0 from real players' combat logs; no bot build
>   has a talent a player could not take.
> - **Quality of life**: old bots are repaired at their next login (level, gear, talents, mounts) instead of being
>   purged; `lfg bot` recruits at the right level; about 10% of free bots spend time in their capital.
>
> One specialization is set aside by default, **Bloodmage Eternal** (`CoaExcludedSpecializations = 99`): it still
> does not play correctly on the current core.

Full history from 1.1 to 2.4: **[CHANGELOG_COA.md](CHANGELOG_COA.md)** (also `CHANGELOG.md` in the CoA Bots package).

## Download

| You have | Take |
|---|---|
| the **CoA Server Manager** | nothing to do: it installs the newest `v*` tag of this module (2.4 = tag `v2.4`) |
| CoA Bots 2.0, 2.1, 2.2 or 2.3 | the **SquidBots launcher**: it shows "Update available", one click (backup, stop, update, restart) |
| CoA Bots 1.9.1, 1.9, 1.8 or the CoA repack of 30/09 | the 2.4 update of the launcher takes these too |
| nothing yet | the **full 2.0 repack** from the launcher, then let it update to 2.4 |

Companions: the **[SquidBots Lite](https://github.com/Zyth45/squidbots-addon)** addon (in the package) and the
**[SquidBots dashboard](https://github.com/Zyth45/squidbots-dashboard)** with its Armory (in the package, and in the
Server Manager's "Dashboard" tab).

**Found a crash?** Open an issue with the newest `.txt` file from the `Crashes` folder next to `worldserver.exe`,
and tell what the bots were doing (class, level, dungeon or open world) if you know.

## Which versions go together

| jealous-sound | This module |
|---|---|
| **`main`** of 15 October 2026 (`9dc61efc`), release main-20261016-2ab3ebca | tag **`v2.4`**, branch `coa` |
| `main` of 12 October 2026 (`d5ff1cfa`), release main-20261012-e91c0624 | tags `v2.1` to `v2.3.1` |
| `main` of 11 October 2026 (`0a0a21d3`), release main-20261011-a543922b | tag `v2.0` |
| repack main-20261007 | tags `v1.9`, `v1.9.1`, `v1.9.2` |
| repack main-20260930 / core of 4 October | tags `v1.8`, `v1.8.1` |
| issue-batch-20260915 (`a8b28faac98d`) | tag `bots-issue-batch-20260915-v1.1`, with the core tag of the same name from [Zyth45/azerothcore-wotlk-coa](https://github.com/Zyth45/azerothcore-wotlk-coa) |

jealous-sound's `main` carries everything the bots need from the core (the mod-playerbots hooks and the CoA
specialization API). **No fork of the core is needed**: the module goes into `modules/`, which the core's
`.gitignore` leaves alone. The CoA Bots packages add fixes for four crashes under load, a missing setting logged once per load and a server
stop that no longer hangs, on branch `coa-bots-2.4` of
[Zyth45/azerothcore-wotlk-coa](https://github.com/Zyth45/azerothcore-wotlk-coa).

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

5. Copy `playerbots.conf.dist` to `playerbots.conf` and set `PlayerbotsDatabaseInfo`. Every other value in the
   reference file is already what this realm wants.

> [!WARNING]
> The build writes `playerbots.conf.dist`. It never writes `playerbots.conf`, and `playerbots.conf` is the file the
> server reads. **An old `playerbots.conf` left over from a previous build keeps its old values in silence**: no
> rotations, no excluded specialization, bots whispering everyone who writes in a channel. Delete it or copy it
> again from the reference. With no `playerbots.conf` at all the server runs on the compiled defaults, which are
> the right ones.

## Recommended settings

| Setting | File | Why |
|---|---|---|
| `CharacterCreating.Disabled.ClassMask = 2047` | worldserver.conf | stops **players** rolling the nine WotLK classes; the bots no longer need it, `AiPlayerbot.CoaClassesOnly` keeps them on CoA classes on its own |
| `MapUpdate.Threads = 8` (half your CPU threads) | worldserver.conf | hundreds of bots need several map threads |
| `AiPlayerbot.MinRandomBots = 200` / `MaxRandomBots = 200` | playerbots.conf | about 3.5 GB of server memory for 200 bots, 10 GB for 1000 |
| `AiPlayerbot.RandomBotMaxLevel = 60` | playerbots.conf | the CoA default: random bots spread over levels 1-60 (80 upstream); set 1 instead to have every bot start at level 1 and level up while playing, which also leaves the level brackets below with nothing to balance |
| `AiPlayerbot.LevelBrackets.Enabled = 1` with `Dynamic.UseDynamicDistribution = 1` | playerbots.conf | the CoA default: bots are rebalanced across 9 level brackets every 5 minutes, narrow up to 30 (1-3 on its own), so about two thirds of them are in 1-30; brackets holding a real player draw more bots (`Dynamic.RealPlayerWeight = 3.0`) |
| `AiPlayerbot.LevelBrackets.FreshStart = 1` | playerbots.conf | the CoA default: a bot moved into the 1-3 bracket comes back at level 1 at its race's starting point, like a new player, and a bot moved to any other level is sent somewhere fitting that level |
| `AiPlayerbot.BotActiveAlone = 60` | playerbots.conf | already the default here: more bots stay active away from players |
| `AiPlayerbot.GroupInvitationPermission = 2` | playerbots.conf | every bot accepts group invites |
| `AiPlayerbot.BotTextLocale = 2` | playerbots.conf | bot chat in French (-1 client locale, 0 English, 3 German, 6 Spanish, 8 Russian) |
| `AiPlayerbot.CoaSpecRotations = 1` | playerbots.conf | **default since v1.4**: bots follow the authored rotation of their specialization on top of the automatic spell choice. 0 leaves only the runtime choice |
| `AiPlayerbot.CoaExcludedSpecializations = 99` | playerbots.conf | **default since v1.4**: specializations bots never take, by id. 99 is Bloodmage Eternal, whose cursed form is granted by nothing in the core. Write the reason and the date next to any id you add |
| `AiPlayerbot.CoaHealsExcluded = "Nanobot Reconstruction"` | playerbots.conf | heals measured to heal nothing, set aside by name. A bot builds its heal kit from every heal it knows, not from its rotation, so removing a rotation line is not enough |
| `AiPlayerbot.CoaGroupTelemetry = 0` | playerbots.conf | **default since v1.4**: at 1 it writes a block to the log after every group fight. Useful to measure a run, dead weight on a realm carrying hundreds of bots |
| `AiPlayerbot.CoaLfgBots = 1` | playerbots.conf | a player typing `lfg bot heal` in the Zone or Newcomers channel gets offers from free bots, with no GM command. `CoaLfgChannels` lists the channels, matched on the start of their name |
| `AiPlayerbot.CoaHealerManaReserve = 35` / `CoaCasterManaReserve = 15` | playerbots.conf | share of its mana a bot keeps for healing instead of spending it on damage: the larger share for healers, the smaller one for anything else that knows a heal; a bot with no heal is never held back, 0 disables it |
| `AiPlayerbot.ZoneChannelId = 3` | playerbots.conf | already the default here: the per-zone channel is numbered 3 on CoA ("Zone - <place>"), 1 on a stock client; with the wrong number bots talk where nobody reads |
| `AiPlayerbot.BroadcastWorldChannelName = "Ascension"` | playerbots.conf | the realm-wide channel's name; set `BroadcastToWorldGlobalChance = 0` to keep bots out of it entirely |
| `Appender.CoaBots=2,4,1,CoaBots.log,a` and `Logger.playerbots.coa=4,CoaBots` | worldserver.conf | optional: every 10 minutes, which CoA spells bots cast and why casts fail |

Known issue: `mod-aoe-loot` crashes the CoA release at the first bot login (its module string is missing from the
release database). Leave it out or import its SQL.

## Commands

Everything below is typed in the game chat. A bot has to be **in your group** to answer most whispers, and a
trailing `?` shows instead of changes: `co ?` lists, `co +name` adds, `co -name` removes.

| Command | What it does |
|---|---|
| `lfg bot tank\|heal\|dps` | **typed in the Zone or Newcomers channel, by any player**: free bots of those roles answer with an offer, which the player accepts by inviting them. No GM command needed |
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

## The addon

**[SquidBots Lite](https://github.com/Zyth45/squidbots-addon)** 2.0 ships with the CoA Bots package, in
`Client/Interface/AddOns/` (the installer and the update copy it into your client). A thin bar of orders and a "+"
that asks for a tank, a healer or damage; the bots' roles on the party frames, with their alerts; the offers bots
whisper as toasts with an Invite button. `/sbl` shows or hides it, `/sbl scale` or Ctrl + wheel resizes it,
`/sbl lang fr|en` switches language. Its version follows CoA Bots and it warns when the server is newer.

Everything it sends is a chat command the module already understands, so it never asks the server for anything a
player could not type.

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
