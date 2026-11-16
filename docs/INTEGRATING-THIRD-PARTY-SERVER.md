# Integrating SquidBots into a third-party server

SquidBots is the bots module of Conquest of Azeroth servers. This page is for a server that builds it into its own core,
with its own races, modules and installer, and wants to follow each release without patching the module.

The rule on our side: every release builds on the official Conquest of Azeroth core, a server without added races
behaves exactly as before, and everything a server needs to adapt is in configuration, not in code.

## 1. Build

- Put the module in `modules/mod-playerbots` and build the core as usual (`MODULES=static`). Pick a release tag
  (`vX.Y.Z`): a tag is what we test. `squidbots-release.json` at the module's root names it.
- Under MSVC, `mod-playerbots.cmake` moves the module's sources to their own static library, `modules-playerbots`,
  because `modules.lib` would pass the 4 GB limit of a COFF library. A core that already makes `modules-playerbots`,
  or that already spreads the module's `.cpp` files over libraries of its own, is left as it is.
- The module only uses core APIs of the official core. Where it can use more, it looks for the header first
  (`__has_include`) and falls back otherwise; the release manifest lists these headers under
  `optional_core_headers`. Today: `AscensionIncarnation.h` (male-only added races) and `AscensionCompanionLoot.h`.
- Dungeon Clear is a separate module with its own releases; it does nothing without SquidBots.

## 2. Databases and SQL

- Three databases: `characters` and `world` (the core's), and `playerbots` (`PlayerbotsDatabaseInfo`, default
  `acore_playerbots`). `data/sql/playerbots/create/create_mysql.sql` creates it.
- `data/sql/<database>/<stage>/*.sql`, applied per database in the stages `base`, `updates`, `custom`, each in name
  order. `archive` is history, never applied.
- By default the module applies them itself at startup (`Playerbots.Updates.EnableDatabases = 1`, the core's
  update system). An installer that applies them itself sets it to 0.
- **A published SQL file never changes, never moves and is never renamed**: a change always comes as a new file. A
  file in `base` always creates its tables. `squidbots-release.json` lists every file with its sha256 and size, so an
  installer can check them before starting; our release step stops if a published file changed.
- `data/sql/playerbots/base/playerbots_travelnode_path.sql` is about 70 MB (`"large": true` in the manifest). Ship it
  with the server: without it, a fresh install has no travel paths for the bots.

## 3. Configuration

- `playerbots.conf.dist` holds every key with its default. A key missing from a server's `playerbots.conf` takes
  the default of the code, which is the same as the `.dist`: a `.conf` made from an older release keeps working,
  and the new options of a release apply with their defaults.
- Key names, table names and SQL folders are stable: we add, we do not rename.
- `conf/playerbots.conf.settings.json` describes the options a server owner usually changes (title, description,
  group, type, default, range, `new_in`). It is regenerated at each release, for settings pages.
- `squidbots-release.json`: `new_keys` and `changed_defaults` against the previous release.
- Useful keys for a third-party server:
  - `AiPlayerbot.Enabled`: 0 ships the module with the bots off.
  - `AiPlayerbot.CoaEconomy.AHBot.Enable = 0` when another auction house bot module is built in: our auction
    house bot and its `.ahbot` commands are then not registered.
  - `AiPlayerbot.DeleteRandomBotAccounts`, `DeleteRandomBotGuilds`, `DeleteRandomBotArenaTeams`: a bot reset at
    the next start; set them back to 0 right after.
  - `AiPlayerbot.CoaStatusFile`: the status file of the SquidBots dashboard.

## 4. Added races

Random bots only take the base races (1 to 8, 10, 11) unless the server lets them have others:

- `CoACustomRaces.BotsEnable = 1` (the custom races module's own switch): every race of `ChrRaces.dbc` that has a
  `playercreateinfo` row may be given to random bots.
- `AiPlayerbot.ExcludedBotRaces = "55,63"` and `AiPlayerbot.CrashingBotRaces`: races never given to bots. These
  two lists always win. `CrashingBotRaces` defaults to the races that crash an unpatched client (32, 72, 77, 97);
  a server whose client draws them can change it.
- One line per race, for anything else. A server adds a line when it adds a race, without touching the module:

```
AiPlayerbot.BotRace.85 = "bots=1 gender=female side=horde look=simple names=orc"
AiPlayerbot.BotRace.96 = "gender=male side=alliance classes=1,2,4,5,8,9"
```

| Field | Values | Without it |
|---|---|---|
| `bots` | `1`: random bots use the race even without `CoACustomRaces.BotsEnable`; `0`: never | base races yes, added races with `BotsEnable` |
| `gender` | `male`, `female`, `any` | the module's list of single-model races, then the core's male-only races, then a coin |
| `side` | `alliance`, `horde`, `dbc` | `ChrRaces.dbc` (TeamID); base races always keep theirs |
| `look` | `full`, `simple` (first face and hair variant only, no facial hair: for missing textures) | `full` |
| `names` | `human`, `orc`, `dwarf`, `nightelf`, `undead`, `tauren`, `gnome`, `troll`, `bloodelf`, `draenei`, `generic` | the race's pool (`generic` for added races) |
| `classes` | class ids, comma separated | every class the race has in `playercreateinfo` |

- Fields are separated by spaces (or `;`); case does not matter. A line for a race that is not in this server's
  `ChrRaces.dbc`, or with an unknown field or value, is ignored as a whole, with an error line at startup. It never
  stops the server and never touches a character.
- The start position, the race mask, the racials and the gear the race may wear come from the core's data
  (`playercreateinfo`, `ChrRaces.dbc`, `getRaceMask()`): a race that players can create works for bots too.
- Bots already created keep their race and gender. Bots of a race closed by a line or a list stay offline; their
  characters are not deleted. Restart after a change.

## 5. Startup check

`AiPlayerbot.StartupCheckOnly = 1` (or `CoA.ValidationMode = 1`): the server loads the module, its settings, its
data and its AI contexts, then creates, deletes, renames and logs in no bot, and the bots' world updates do nothing.
For a qualification run that checks a build starts before it is opened. Dungeon Clear is inert in this mode too.
Default 0.

## 6. Following our releases

- Take a tag, read `squidbots-release.json` (new keys, SQL files and their sha256, the addon and Dungeon Clear
  versions tested with it when given) and `changelog.d/`.
- A server that needs a change in the module is welcome to tell us: we would rather add a key than see a patch
  that has to be redone at each release.
