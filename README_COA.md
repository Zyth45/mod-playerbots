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
> **Out of date, kept for the record.** This tag is the v1.1 release; it warned that bots were only tested on
> levels 1 to 25. Since 19 September 2026 bots run over the whole 1-60 range, checked on a realm of 1000 bots
> across every level. See the `coa` branch for the current state.

My own server runs 24/7 and my bots level up naturally, so I can watch them and fix problems as they reach higher
levels. If too many different spells crash at once, I will let the bots level up (for example to level 40) and fix
a whole level range in one go.

**Found a crash?** Open an issue with the newest `.txt` file from the `Crashes` folder next to `worldserver.exe`,
and tell what the bots were doing (class, level, dungeon or open world) if you know.

## Which versions go together

The core and the module must come from the **same tag**. Each tag matches one jealous-sound release.

| jealous-sound release | Tag (core and module) |
|---|---|
| issue-batch-20260915 (`a8b28faac98d`) | `bots-issue-batch-20260915-v1.1` (latest, same code as the CoA Bots v1.1 zip) |
| issue-batch-20260915 (`a8b28faac98d`) | `bots-issue-batch-20260915` (v1.0) |

The core needs the mod-playerbots core hooks and a small CoA specialization API. Until they are part of
jealous-sound's repository, use [Zyth45/azerothcore-wotlk-coa](https://github.com/Zyth45/azerothcore-wotlk-coa):

- tag `bots-issue-batch-20260915-v1.1` (branch `coa-bots`): the jealous-sound release, the core hooks, the
  specialization API and fixes for crashes that bots trigger often. **Use this one.**
- branch `playerbots-support`: only the core hooks and the specialization API (the changes proposed to
  jealous-sound).

## Build

1. Get the core and this module:

   ```bash
   git clone --branch bots-issue-batch-20260915-v1.1 https://github.com/Zyth45/azerothcore-wotlk-coa.git
   git clone --branch bots-issue-batch-20260915-v1.1 https://github.com/Zyth45/mod-playerbots.git azerothcore-wotlk-coa/modules/mod-playerbots
   ```

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
| `CharacterCreating.Disabled.ClassMask = 2047` | worldserver.conf | random bots are created with CoA classes only |
| `MapUpdate.Threads = 8` (half your CPU threads) | worldserver.conf | hundreds of bots need several map threads |
| `AiPlayerbot.MinRandomBots = 200` / `MaxRandomBots = 200` | playerbots.conf | about 7 GB RAM for 200 bots |
| `AiPlayerbot.RandomBotMaxLevel = 1` | playerbots.conf | bots start at level 1 and level up while playing (default: random levels up to 80) |
| `AiPlayerbot.BotActiveAlone = 60` | playerbots.conf | more bots stay active away from players |
| `AiPlayerbot.GroupInvitationPermission = 2` | playerbots.conf | every bot accepts group invites |
| `AiPlayerbot.BotTextLocale = 2` | playerbots.conf | bot chat in French (-1 client locale, 0 English, 3 German, 6 Spanish, 8 Russian) |
| `Appender.CoaBots=2,4,1,CoaBots.log,a` and `Logger.playerbots.coa=4,CoaBots` | worldserver.conf | optional: every 10 minutes, which CoA spells bots cast and why casts fail |

Known issue: `mod-aoe-loot` crashes the CoA release at the first bot login (its module string is missing from the
release database). Leave it out or import its SQL.

## Updating to a new jealous-sound release

The bot work is a few commits on top of the release in each repository. Cherry-pick them onto the new release
revision (`targetSourceRevision` in the release `UPDATE.json`), rebuild, and tag `bots-<release id>`.

## Credits

- [jealous-sound](https://github.com/jealous-sound/azerothcore-wotlk-coa) for the Conquest of Azeroth server
- [AzerothCore](https://github.com/azerothcore/azerothcore-wotlk) and
  [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots)
- [ascensionsidekick.com](https://ascensionsidekick.com) for the CoA specialization roles and talent builds

Same license as mod-playerbots (GPL-2.0).
