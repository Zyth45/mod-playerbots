# CoA Bots — changelog

## v2.8.3 (30 October 2026)

- **Alts as a team** (with SquidBots Lite 2.8.3): new addon channel `SQBA` (contract in the release notes). Opening a quest
  at a quest giver sends each alt's state (can take, missing earlier quest of the chain, level, class/race, reputation,
  skill, done); alts wait for "take" instead of taking the quest on their own while the addon is there.
- **Quest chain catch-up** (`AiPlayerbot.CatchUpQuestChains = 1`): an alt blocked only by the earlier quests of a chain marks
  them rewarded (no reward, no item) and can take the quest. Never for level, race, class, reputation or skill.
- **Quiet alts** (`AiPlayerbot.AltChatToAddon = 1`): with the addon, the repeated quest and trainer whispers go to the addon.
- **Trainers**: `alt train` / `alt learn` list and learn what an alt can learn at the trainer or Book of Artisans in use.
- **Team bar and quest objectives** for the addon (TEAM, OBJ messages, on change only).
- **High Risk missions**: `mission gather ... risk` (`AiPlayerbot.Missions.HighRisk`, `RiskMailEvery`, `RiskMailMinutes`).
- **Sell junk** (`s junk`): grey items plus white and green gear the bot cannot use or does not need; one summary line
  instead of one whisper per item for every batch sale.
- **Stale targets**: unit values keep the GUID and find the unit again on the bot's map at every read (a despawned
  creature or a player who logged out crashed AttackAction through a freed pointer).
- **Guild bank deposits on by default** (`AiPlayerbot.GuildBankDeposit.Enable = 1`), items `ore,herb,leather,cloth,boe`;
  new category `boe`: bind-on-equip gear, green or better, the bot can never wear.
- **Five deaths**: a bot that died five times rises at the spirit healer instead of on its corpse next to the same mobs.
- **Raids**: Molten Core Gehennas (bots leave his Flame Orb cast), Zul'Gurub Hakkar positioning and Blood Siphon.

## v2.8.2

- **Random bot transmog**: half of the random bots (`AiPlayerbot.RandomBotTransmogPercent = 50`) now show other
  models of their armor and weapon types, a whole item set when one fits their level, from level 10
  (`RandomBotTransmogMinLevel`). Purely visual: their real gear and stats never change and nothing is saved; a bot keeps
  its look at every login. Not used on alt bots or self-found levels. `AiPlayerbot.RandomBotTransmog = 0` turns it off.
- **Duels in front of Orgrimmar and Stormwind**: a few random bots now duel outside the main gates, Horde bots at
  Orgrimmar and Alliance bots at Stormwind, in pairs of close levels (by default two pairs near level 60 and one near
  level 30), and are replaced by other bots every 20 minutes. On by default; `AiPlayerbot.CityDuels.Enable = 0` turns
  it off, `AiPlayerbot.CityDuels.*` sets the number of bots, their levels and the places. Bots of a player's group
  are never taken, and no duel starts while a player of the other faction is near.
- **Gathering missions for your alts**: whisper `mission gather [minutes] [mining|herbalism]` to one of your own
  characters played as a bot. It leaves the group, gathers ore or herbs in a zone of its level on its continent, and
  mails you what it gathered when the time is up, its bags are full or you whisper `mission stop` (`mission status`
  answers `MISSION;state;zone;minutes left;items;profession;deaths`). The mission resumes when the alt logs in again,
  even after a server restart. Never for random bots, no selling during a mission. Settings: `AiPlayerbot.Missions.*`.
- **Automatic guild bank deposits** (off by default, `AiPlayerbot.GuildBankDeposit.Enable = 1`): a bot in the guild of
  a real player (your alts, the bots you invited) that stands near a guild vault deposits its trade goods (ore, herbs,
  leather, cloth...; `GuildBankDeposit.Items` sets the classes) and its gold above `KeepGold` (100 by default), with
  no command. It needs the deposit right of its rank on the tab (`GuildBankDeposit.Tab`, else the first allowed one)
  and room in it; bound, quest and equipped items are never moved. Never in random bot guilds, never during a mission.

## v2.8.1 — Latest CoA core, classic dungeon bosses, battleground fixes (October 2026)

### Highlights
- **Latest CoA core**: 284 new commits from the public CoA core since 2.8 (full list in the GitHub release notes).
- **Boss mechanics for the classic dungeons**: bots now handle the bosses of Wailing Caverns, Razorfen Kraul, Razorfen
  Downs, Blackfathom Deeps, Scarlet Monastery, Uldaman, Blackrock Depths, Blackrock Spire, Stratholme, Scholomance and
  Maraudon. Thanks to Brytenwally (#45).
- **Molten Core is back on Heroic, Mythic and Ascended** for servers whose database was created before 11 October.
- **Self-found bots (optional, off by default)**: with `AiPlayerbot.CoaSelfFound = 1`, bots wear only what they loot,
  earn from quests or are traded, and random bots level only by playing, up to level 59 by default
  (`CoaSelfFoundMaxLevel`); at 60 bots are geared as usual for endgame. Existing levels and gear are kept.

### Latest CoA core
2.8.1 includes 284 new commits of the public CoA core since 2.8, among them:
- creature magic resistance and the new open-world creature damage, on by default;
- PvE Power and PvP Power, on by default;
- cross-faction battleground queues (mod-cfbg), on by default;
- Road to De Other Side (map 937) restored;
- name filter for profanity;
- account-wide friends, opt-in;
- Wisdomball dungeon quest fixes;
- gathering nodes moved to their Ascension positions;
- large batches of dungeon, item, quest and class fixes;
- many quest and database fixes.

### Fixes
- **"Loot only" bots**: with "Your own bots can gear themselves" off in the launcher, the bots in your group no longer
  gear themselves at all (level up, `.levelup`, specialization changes, `autogear`): they wear what they loot, earn from
  quests or get from you. With it on, "Best gear quality of the bots" now also caps the gear they put on at level up.
- **Molten Core** was empty on Heroic, Mythic and Ascended on databases created before 11 October: its spawn masks are
  restored.
- **Battlegrounds**: safety nets against world server crashes (battleground objects and creatures looked up within
  bounds, end of battleground scores guarded), and the bots' battleground tactics only run on the battleground's own
  map. Thanks to nightlund (#52).
- Manastorm: the Horde teleport pad fixed in 2.8 stays in place.

### Bots
- **Bloodmage Eternal** tanks enter their form, keep Hypovolemic Shock up on packs and no longer cast Sanguine Rupture
  (#38, #51).
- **Classic dungeons**: boss mechanics for Wailing Caverns, Razorfen Kraul, Razorfen Downs, Blackfathom Deeps, Scarlet
  Monastery, Uldaman, Blackrock Depths, Blackrock Spire, Stratholme, Scholomance and Maraudon. Thanks to Brytenwally (#45).
- **Road to De Other Side**: boss strategy for the bots. Thanks to steviecraycray (#49).
- Custom race exclusions now also apply to existing random bots, not only to new ones. Thanks to Corfirean (#54).
- Wildcard Hero bot files carry the GPL header. Thanks to shiro2448 (#48).

### Dungeon Clear 2.8.1
- Blackrock Depths, Lyceum: after a fight pulls the tank away, it returns to the brazier so the torch can be lit.
- Stratholme: the Slaughterhouse event no longer ends early when Ramstein opens the gate; the group waits for the
  portcullis during the Barthilas fight instead of stopping.
- Shadowfang Keep: if the courtyard door closes behind the group while Rethilgore is still alive, it is opened again.
- Bots that fall behind between pulls are brought back to the tank after 25 s instead of 60 s, so Heroic and Mythic runs
  are about 10 minutes faster (bots only, never players).
- New setting: `DungeonClear.StrandedRecoveryWaitSecs = 25` (capped by `DungeonClear.StrandedRecoveryNoProgressSecs`).

### Launcher 0.3.48
- On a new install, the navigation maps can be downloaded instead of generated.

### Known limits
- Dungeon Clear: Blackfathom Deeps (two bosses) and the Scarlet Cathedral (Whitemane does not resurrect Mograine) can
  still get stuck. Heroic and Mythic mostly depend on bot gear.
- Dungeon Clear: a Heroic or Mythic clear still running at the daily instance reset (04:00 server time) gets stuck. Type
  `.dc off`, then start a new clear.
- If bots stay behind during a clear, the Regroup button teleports them back.

## v2.8 — A smoother server with many bots, Dungeon Clear, realm cards (October 2026)

### Highlights
- **Much smoother server with many bots.** A hidden database wait in the Recruit-a-Friend reward check froze every map thread
  for 2-7 s every minute (and up to 35 s under load). It is now asynchronous and skipped for bots. On our test machine, with
  the same load (9 heavy dungeon runs + benches): spikes over 2 s went from 24 per hour to 1 per hour, worst p99 tick from
  32.7 s to 0.14 s. The same machine now handles **75 simultaneous bot dungeon runs** without a single spike over 1 s
  (it collapsed at 9 before).
- **New optional module: Dungeon Clear.** Your bot group can clear a dungeon on its own, boss after boss, in Normal, Heroic
  and Mythic 0. Start it from the SquidBots Lite addon (Dungeon button) or with `.dc on` inside a dungeon. It only acts on
  groups that started it. On by default on the CoA realm; switch it off in the launcher (Modules). Not available on Wildcard and Reborn yet.
- **Realm cards like Ascension**: each realm shows its own background, title and description in the realm list
  (Vol'jin for CoA, Darkmoon for Wildcard, Bronzebeard - Warcraft Reborn).

### Performance (no gameplay change)
- Recruit-a-Friend reward check no longer blocks the map threads (see above).
- Destiny Weaver no longer rebuilds strings or re-reads its config on every creature update; config is read once and on
  `.reload config`.
- Tinker scripts no longer allocate their state on every player update.
- The server no longer builds party stats and aura updates for bots that have no game client.
- Bots no longer store achievement progress. The launcher removes the old bot rows once after updating (backup first; on our
  test server: 11.7 million rows, 757 MB). You can run it again from Settings > Backups.

### Stability
- Fixed a world server crash when a bot was flagged "in battleground" while its battleground was already gone.
- Includes the latest core fixes from upstream, among them three world server crashes found in production dumps.
- Random Heroic / Mythic dungeon queues no longer turn grey after changing map (the server sends the released difficulties
  again on every map change, not only at login).

### Bots
- Random level-60 bots get fewer downgraded items: `AiPlayerbot.RandomGearLoweringChance` default 0.3 → 0.1 (existing
  configs keep their value).
- Bots set to "stay" when their master released his spirit follow again as soon as the master is alive.
- Tinker Invention no longer loops on Mana Module; Mechano-Bear is no longer rebuilt while it is alive.
- Less noise in the playerbots log ("No spellid").
- Magtheridon: channeler kill order kept; timers no longer re-run every tick while caged.
- Maiden of Virtue: tanks reach a stunned healer up to 23 yd away.
- Offensive healers stop mana-costing attacks below 25 % mana in raids.
- Tanks no longer taunt enemies immune to taunt.
- Thanks to ladiabale for the detailed raid reports.

### Auction house
- The buyer bot can settle a bid at once: the sale ends and the gold arrives by mail right away instead of 12-48 h later
  (`AuctionHouseBot.Buyer.InstantBids`, on in the CoA Bots config).
- Launcher: buyer and auction house size settings (buyer on/off, pace, price paid, CoA prices, per-seller cap, instant
  payment, size), applied live when the server runs.

### Launcher 0.3.45
- Dungeon Clear card in Modules and per realm; realm cards; achievement clean-up; auction house settings.
- Stop button for a single service, "Like Rexxar" card, and a "What's new" window.

### SquidBots Lite 2.8
- The Dungeon button opens the Dungeon Clear panel (also `/sbl dc`): run state, On / Off / Pause / Skip, pull mode, and the boss list with a Go button.

### Known limits
- Dungeon Clear: Blackfathom Deeps (two bosses) and Sunken Temple (Shade of Eranikus) are not yet cleared reliably in
  Normal. Heroic and Mythic mostly depend on bot gear.
- If bots stay behind during a clear, the Regroup button teleports them back.

## v2.7 — Smarter bots in raids, Reborn bots with mystic enchants (October 2026)

Many of these fixes come from the raid and dungeon reports of ladiabale and Yeh: thank you both.

### All realms
- Melee bots: Auto Attack no longer blocks the rotation (CoA melee specializations gain 30 to 55 % damage).
- Healers follow the tank who is taking the hits and stop dealing damage while a tank is below 60 % health; in raids
  they no longer run back and forth between their raid spot and the tank.
- A dead main tank is no longer kept as main tank; `coa raid` sets the tank marker. Auto pull is off in raids and on
  bosses.
- Ground effects: bots step out of them, no longer stand in lava, and in Zul'Gurub reach their target from a spot
  outside them.
- Raids: Magtheridon's cubes, Karazhan (Maiden, Illhoof, Nightbane, Enfeeble, one encounter reset when leaving
  combat), Gruul (Shatter, Cave In), Naj'entus (a single bot throws the spine).
- Tempest Keep: CoA tanks taunt in raids (also in Zul'Aman and Molten Core), even when another tank holds the enemy.
  Al'ar: Embers are dealt with once pulled out of the group, everyone steps back 20 m, Rebirth is avoided. Void Reaver:
  orbs dodged in a straight line. Solarian: healers finish their casts, the Wrath carrier uses a defensive. Kael'thas:
  his weapons are only used when they can be, Pyroblast can be interrupted, mind control is broken with the blade,
  assigned tanks taunt Sanguinar and Kael'thas, Capernian is taken by a ranged damage dealer when there is no
  warlock. Healers no longer run back to the tank during Al'ar, Void Reaver and Solarian.
- Sunwell Plateau, Kalecgos: bots rotate through the spectral realm by Arcane Buffet stacks (never the Kalecgos tank
  or a bot under Spectral Exhaustion), attack Sathrovarr there, and stay on Kalecgos' flanks away from Tail Lash and
  Frost Breath.
- Resurrection: a bot keeps its claim on a body out of sight and no longer teleports while walking to it.
- A bot far from you also comes back during your fight; an enemy on another floor and out of sight is no longer a
  target; in arenas a bot goes after a target hidden behind a pillar.
- An immune boss no longer makes a bot leave combat; zone-only potions are no longer tried everywhere; low-level bots
  no longer wear abnormally armored necklaces; group buffs are no longer held back by threat.
- Crash fixed: a quest giver despawning while a bot looked for it crashed the map.
- New settings: `AiPlayerbot.CoaReleaseDelay` (120), `AiPlayerbot.CoaWcrMysticEnchants` (1),
  `AiPlayerbot.CoaGroupTelemetryRaids` (0), `AiPlayerbot.CoaLogTargets` (0).

### Conquest of Azeroth
- CoA rotations cleaned (Witch Hunter, Healing Ward / Spiritual Recall, Pyromancer healer, Destructo-Bot) and raid
  rotations corrected from real raid logs (51 dead lines removed, 26 corrected, 16 dropped).

### Warcraft Reborn
- Mystic enchants: each specialization gets its own set (1 legendary, 5 epic, 10 rare), applied as the bot levels up
  and when it changes specialization (+20 % damage on our bench).
- Reborn tanks now tank: they take their threat stance (Defensive Stance, Righteous Fury) and taunt; warriors use
  Shield Block, Revenge and Shield Wall, Protection paladins keep Holy Shield up, Blood death knights cast Death and
  Decay whenever it is ready.

## v2.6 — Three realms: Conquest of Azeroth, Wildcard and Warcraft Reborn (October 2026)

### New realm modes
- **Warcraft Reborn realms** (`CoA.ClassModel = "wcr"`): random bots are the original classes (Warrior to Druid,
  Death Knight included). They learn the Reborn spells like a player at the trainer, take their talents through
  Character Advancement with a build for each of their three trees, and their tree gives their role (tank, healer,
  damage) for lfg bot, the raid builder and the dungeon finder. Whisper `wcr` to see a bot's tree, `wcr spec 0|1|2`
  to change it; game masters: `.playerbots coa wcr [spec N|reload]`.
- **Wildcard realms** (Darkmoon): Heroes of any role can be recruited by lfg bot and the raid builder (a recruited
  Hero rebuilds its draft for the role), a Hero keeps its role from one draft to the next and regears after a
  redraft, Heroes hold real weapons (a bow or a gun when they fight at range), and the PvP tactics apply to them.
  Whisper `wildcard`, `wildcard role tank|heal|dps`, `wildcard redraft`.

### Fixes
- A spell that triggers itself no longer sends the gear rating into an endless loop (server crash).
- Bots of one class no longer all take the same tree on a Warcraft Reborn realm.
- Warcraft Reborn: shamans set their totems again (Call of the Elements no longer loops on an empty bar), hunters
  keep their Auto Shot going, Frost and Unholy death knights spend their spare runes, a Blood tank stays in Frost
  Presence and holds threat, rogues spend their combo points on Eviscerate instead of Expose Armor.
- Wildcard: damage dealers are spread over melee, ranged and caster builds (they were nearly all melee Agility),
  draft toward the Season 10 archetypes, always start with an ability they can use without a cooldown, and a tank
  with a taunt; Heroes learn and cast the higher ranks of their abilities and ranged Heroes shoot with Auto Shot.

### Loot and gear
- **Best gear** (`autogear`, "Best gear for the group" in SquidBots Lite) keeps an equipped **heirloom**: it no
  longer swaps it for a worldforged piece and leaves the heirloom in the bags. `autogear reset` still regears
  everything from scratch.

## v2.5 — Auction house, Manastorm, smarter casts and your reports (October 2026)

### Core (repack)
- **New map data** from the CoA dev team: the maps, vmaps and mmaps of the CoA custom zones the repack lacked
  (bots fell through the ground there) and fuller paths for the bots. It makes 2.5 a **big update** (about 2 GB to
  download, 5.3 GB of map files): the old map folders are set aside in the update's backup, and Undo puts them back.
- **Auction house bot** (mod-ah-bot-plus), **on by default**: a seller (Gavelgrin) keeps about 3000 auctions of items
  you can get in game, nothing above level 60 and no TBC/WotLK goods, and buys what players sell. Turn it off with
  `AuctionHouseBot.Enable = false` in `configs\modules\mod_ahbot.conf` (server stopped).
- **Manastorm Endless**, **off by default** (`Manastorm.Enable = 0` in `configs\modules\manastorm-endless.conf`):
  its tables and data are always installed (the server needs them at start); to play it, the game client needs the
  Manastorm client files (Interface files of the CoA client patch). Optional core feature, not supported by the CoA
  Bots team.
- The core's one-button rotation assist is **off** (`CoA.RotationAssist.Enable = 0` in `coa.conf`). Optional core
  feature, not supported by the CoA Bots team.

### SquidBots launcher 0.3.25
- **Modules** tab: the auction house (on by default) with an **Empty the auction house** button (only the bot's
  auctions go, bidders are refunded); Manastorm Endless and the rotation assist (off by default), with a notice when
  Manastorm is switched on (game client from the official PTR launcher).
- **Undo the last update**: Management > Backups puts the server back exactly as before the last CoA Bots update
  (database and files), not only after an error.
- "The bots are arriving: X / 500" while they log in after a start, so a high tick at start is no longer alarming.
- Smoother updates: the server restarts by itself after an update if it was running, the test start shows a plain
  sentence with a timer, the download size is right.
- **Your game**: SquidBots Lite 2.5 is detected and installed; the realmlist check and the home page show the address
  players really use.
- Server settings: a "Creature difficulty" section explains the CoA scaling keys (off in CoA Bots).
- **Big updates made clear**: before you click, the update window says what it replaces (here the new map files),
  how much it downloads and how much free disk space it needs; not enough space = no update started.
- **Large updates in several parts**: the 2.5 (with the new maps) comes in 3 files, downloaded and checked one by
  one, in one click.
- New "Unpacking the files" step (part 1 of 3...), and every long step shows "Still working (time)... do not close the
  launcher".
- Verification: a few low-level bots in Outland is now a simple info, with a clearer text.
- Fixes: servers opened to the network are no longer reset to 127.0.0.1; a crash counts once and can be put away;
  clearer messages when a launcher update fails or the bots are missing from a repack. Green/red module switches.

### Combat
- Bots no longer cast a spell their target would reflect (Warlord Kalithresh's 6 s full reflect sent back about a
  hundred casts in one Steamvault run); a cast in progress is cut when the target raises a reflect.
- A group member under a boss's mind control (Blackheart the Inciter) is no longer attacked by the other bots, and a
  charmed bot no longer runs its own actions against its charmer.
- Spells for the bot itself or its friends on a rotation line (Mana Module, Auto Resuscitation Device, Essence of
  Malygos, Void Shield) are cast on the bot instead of being aimed at the enemy, which never worked.
- Feared, stunned, confused or silenced, a bot no longer tries the spells it cannot use in that state (310 heals tried
  under Hellmaw's fear) and no longer sets them aside for later.
- Rotations: Cultist Dreadnought taunts and throws Twilight Shieldtoss from range and uses Sanity Tap when low on mana;
  Tinker Invention no longer builds Rusthound (6 s cast) in fights; Knight of Xoroth Defiance spends 6 Demonfire on
  Suffuse and Melt and keeps Burning Rage / Demon Heart up; Sun Cleric Blessings casts Holy Form, Circle of Valor,
  Blessing of Light, Chosen of the Light, its Vows and Dawn at 20 stacks; Primalist Mountain King casts Ursoc's
  Bellow.
- Sun Cleric Blessings takes Final Vows instead of Taking Vows at level 50.
- Out of combat, bots no longer keep recasting buffs of under a minute, fight cooldowns (a Bloodmage Fleshweaver cast
  Scarlet Delirium again and again between fights).

### Dungeons
- In a dungeon, a dead bot no longer waits for ever: once the fight is over it releases when no living healer of
  the group is inside, or after two minutes if nobody raised it (#37). On a Manastorm floor it always waits: the
  floor raises its dead when it is cleared.
- **Area spells no longer pull the next pack** (`AiPlayerbot.CoaAoeNoIdlePull`, on by default): in a dungeon or a
  raid, a bot no longer casts an area spell that would also hit a creature not yet fighting (a tank's Thunder Clap
  pulled the Ring of Law spectators in Blackrock Depths).
- Razorfen Kraul, Taragaman (Heroic/Mythic): the bots hide behind the earth pillar before Massive Fire Nova, the tank
  once the cast starts.
- Scarlet Monastery, Arcanist Doan: the bots run out of Detonation (36 yd) as soon as the Arcane Bubble is up.
- Random bots join a random dungeon only within 5 levels of the real players waiting for it.

### Loot and gear
- No epic gear below `AiPlayerbot.CoaEpicGearMinLevel` however the bot is geared: bots you add and autogear now keep
  to it too (epics at level 13, #39).
- Your own characters added as bots can spend their free talent points by themselves along their level build
  (`AiPlayerbot.AutoPickTalentsAltBots`, off by default; "talents autopick" does it on demand).
- Heirlooms and other level-scaled items (Felforged sets) are rated by their real values at the bot's level.
- Optional: `AiPlayerbot.LootNeedLevelLookahead` (off by default) lets bots NEED and keep gear a few levels above
  them, to wear it when they reach its level.

### Players and bots
- Party frames are refreshed a few seconds after your own bots log in: no more "Unknown" or "Offline" until /reload.
- Random bots no longer take again the quests they have just dropped to free their quest log (accept / drop loops).

### Server
- Optional: `AiPlayerbot.RandomBotSaveInterval` (seconds, 0 = unchanged) spaces out the autosave of random bots
  without a real master: fewer database writes with thousands of bots.
- The spawn of a creature is found in memory instead of a world database query on the map thread.

### SquidBots Lite 2.5 (addon)
- Role icons, the bot menu and the alerts on Ascension's raid-style party frames too (`/sbl raidframes off` to
  stop).
- Role icons cut cleanly from Ascension's role texture; a bot joining from the dungeon finder gets its role too.
- **Bots > Take my quests**: every bot next to the quest giver takes your quests, alts included (`accept *`).

## v2.4 — Dungeon fixes, players replace bots, every module's settings (October 2026)

### Players and bots
- **Real players take the place of random bots** (`AiPlayerbot.CoaPlayersReplaceBots`, off by default): at 100,
  500 bots and 40 players online leave 460 bots; `AiPlayerbot.CoaPlayersReplaceBotsMin` keeps a floor. Idea by
  CWO4PapaBear.
- Bots are ready for the added races edition (later): with this core they keep the usual races, nothing changes.

### Server and repack
- Core on the latest Conquest of Azeroth main (15/10): about 95 new fixes since 2.3, plus fixes for four crashes
  seen under a heavy bot load.
- No more log flood: every module settings file is installed where the server reads it (Mythic+ included), the
  settings new cores add are filled in with their defaults, a missing setting is reported once per load, the
  playerbots log keeps warnings only, and a console log over 50 MB is set aside at start.
- SquidBots Lite 2.4: "Ask for all three" keeps a tank and a healer among the offers.

### Tanks and dungeons
- **The tank walks its route again**: the guard that keeps bots from walking into an idle pack in a dungeon
  (`AiPlayerbot.CoaBodyPullGuard`, 2.2) no longer applies to the tank, who goes to the packs it means to pull. A tank
  leading a group of bots stood still off its route until the run timed out. In a group led by a real player the
  guard is off for every bot: nearly every step in a dungeon room came within its distance, so the bots stayed
  behind, then ran through the packs to the player once he fought.
- A tank always takes back an enemy hitting one of its group, however far from the healer.
- **Melee out of the boss's cone**: behind or beside a creature, melee bots stand more than 2 yd from its centre, where
  a cone hits at any angle (a Barbarian died behind Tinkerer Gizlock to every Dragon Gun). Not against players.
  The cone dodge also steps out when a bot stands that close.
- In a dungeon, damage dealers and healers no longer open on an idle creature while their group has a living tank:
  the tank pulls (Houndmaster's hounds pulled whole packs, Blood Steward included).

### Battlegrounds
- Random bots leave a battleground queue that will not start: 2 minutes after the last real player left it, or after
  10 minutes in any case. Bots had filled a queue to 26/20 for two players who then left it, and the next players of
  that bracket could not get a battle.

### All classes
- Buffs on a cooldown are cast when ready again: a buff shorter than its cooldown was set aside as "never lands" and
  came 15-30 s late (Unbridled Rage, Light's Hope).
- Buff lines skip spells the bot has not learned yet, and rotation lines skip a spell the bot lacks the power for
  (they failed every tick before the next line could run).
- A cleave or cone around the caster is no longer cast from beyond its reach (Celestial Cleave from 20 yd).
- Purges only aim at enemies within their own reach (Gavel of Wrath, 5 yd, was aimed at 30 yd).
- Buffs are no longer held by the threat hold (Light's Hope, which lowers threat, was held 530 times in two runs).
- A silenced bot no longer counts on its dispels, kicks and other silenced spells as ready.
- The attack kit skips a self-aura the bot already wears (Searing Quiver) and cleanses that deal no damage
  (Resonance Rune).
- Rotations: Break, Condemn and Blade of Faith in melee only; Guardian tanks enchant Spiked Reinforcement first.

### Rotations (`zz_coa_rotation_corrections_2_4.sql`, default rotations only, rows chosen by content)
- **Guardian Vanguard / Reaper Domination** (tanks): Shield Challenge and Writhe taunt at 30 yd, Unyielding Stand on
  packs; no more Reclaim Standards right after placing the standard (Vanguard, Gladiator).
- **Reaper**: Soul Strike and Requiem before Reap; no single-target Rites aimed at the enemy.
- **Ranger**: Survival Potion under low health, not at the pull; Archery drops its stealth-only spells.
- **Sun Cleric Blessings**: Bless and Daybreak on the wounded ally, Ascension on group damage; no Vow of Grace,
  Radiance or Blazing Chariot lines; Gavel of Light in melee only.
- **Starcaller Sentinel**: Moon Arrow at medium mana. **Witch Hunter**: no Infiltrate in rotations, no Stoicism with
  nothing on the bot. **Chronomancer Infinite**: Discordance higher. Dead lines removed (Templar Chastise, Barbarian
  Whirling Advance, Runemaster Palm Sigil: Fire, Incarnation of Chaos, Wand of Time).

### Rotations — from the specs site reports (thanks kuribo)
- **Cultist Heretic / Sun Cleric Blessings** (healers): Abyssal Covenant and Bless kept on the tank, so it gets the
  extra healing; Bless on the wounded ally stays below.
- **Sun Cleric Blessings**: no Shoot, Wand, Horusath Blast, Gavel of Wrath or Chains of Light; Ascension only on group
  damage; Gavel of Grace only when out of mana; Dawnfall on big packs; Devotion of Grace when missing; Paragon and
  Solar Prayer when the healer itself is low.
- **Knight of Xoroth Defiance** (tank): Flesh Hook on a single target only, Meatsaw on packs, Shieldgore on cooldown,
  Chains of Malice to take a target back; Black Shield, Sacrificial Circle and Juggernaut kept for low health, one at
  a time instead of on cooldown.
- **Witch Doctor Shadowhunting**: Shadowflare put back only when missing instead of cast again and again on packs.
- **Venomancer Rot and Vizier**: Weaver Form instead of Spider Form. With the 2.4 core, Spider Form forbids their spells and a bot in it only cast Blight Venom.

## v2.3.1 — Version command (October 2026)

- **`.playerbots version`** gives the CoA Bots version and its source, for players and the console: the login
  message says it too, but a host may have removed it (keep it on a public server, the GPLv2 asks for the link).
- The realm message on how to ask the bots for a healer, a tank or damage now starts with **[SquidBots]**.

## v2.3 — Your reports, better cleanses and healers, smarter PvP (October 2026)

Many fixes from ladiabale's test runs and pull requests (thanks!).

### Rotations
- **Chronomancer Artificer and Infinite**: no more Time Out! as a filler, a 6 s self-stun cast with full mana
  (Zyth45/mod-playerbots#35, ladiabale).
- **Guardian** (all three specs): no more Assume Peak Posture, a cosmetic aura that any movement ends; the bot
  stood still in combat to cast it, and out of combat it was taken for a stance (ladiabale).
- Shipped as `zz_coa_rotation_corrections_2_2_2_time_out.sql` and `zz_coa_rotation_corrections_2_2_2_peak_posture.sql`
  (default rotations only, rows chosen by content, re-runnable). Restart the worldserver to see the change.

### Rotations — from the specs site reports (thanks kuribo)
- **Witch Doctor Shadowhunting**: Frenzied Spirits and Spirit Eclipse at 5 stacks of Spirit, no Hex of Malice nor
  Malefic Wrath, Shadow Avatar up first.
- **Primalist Mountain King** (tank): Earthen Avatar under low health, Bearskin and Primal Convergence under critical
  health instead of all on cooldown at once; Ursoc's Bellow kept up on the target.
- **Venomancer Fortitude** (tank): Weakening Venom instead of Adrenal Venom, Lifeblood under low health.
- Shipped as `zz_coa_rotation_corrections_2_3.sql`.

### Healers and cleanses (from ladiabale's Zul'Farrak and Sunken Temple runs)
- **Resonance Rune** (Runemaster) is used to cleanse allies: it was taken for an attack only because it also purges
  the enemies in its area, so during Hex on the healer the one cleanse the group had was never cast on her.
- A cleanse is only tried when one able to clear the debuff is ready: a Ranger whose self-cleanse was on cooldown
  failed it over and over without casting anything.
- A healer with no heal over time known no longer keeps trying one on the tank: a Fleshweaver did, and nothing
  answered the tank between 65 and 85% health.

### Fixes
- **Pyromancer Draconic** bots carrying the hidden Draconic Form aura (704262) take their Draconic Aspect form again:
  that hidden marker shares the form's spell category and counted as a buff already worn (thanks mightyrathalos).

### Bots
- **Lower spell ranks usable again** after a bot is rebuilt at a lower level: the rank kept below a removed one
  stayed inactive until the next login, and its rotation rows failed (ladiabale).
- **CoA class bots no longer get a miscellaneous item or a fishing pole as their weapon** from the gear factory,
  and give one up at their next gear-up: their weapon strikes failed with it (ladiabale).
- **Preferred mounts** (`mount <spell>`) are found again when the playerbots database has another name than
  `acore_playerbots`, and survive a restart (ladiabale).
- **Alts no longer run circles around you**: the "ranged and healers behind the tank" placement from 2.2 could take
  you for the tank and followed your facing out of combat. It is now off by default (`CoaHoldBehindTank = 1` to try
  it, combat only).
- **Battlegrounds**: a bot hit by a player while capturing an Arathi Basin (or other base) flag fights back instead
  of restarting the capture under the hits.

### Combat, arenas and group play
Each behaviour change has its own switch in `playerbots.conf` (all on by default, `0` turns one off).
- **Stability**: a pair of settings written the wrong way round (MaxRandomBots under MinRandomBots, and the other
  min/max ranges) no longer stops the server when the bots load: the two values are swapped, with an error in the
  log; level rolls and the empty mount or travel lists can no longer draw out of range. Bots stuck mounted after a
  taxi flight are freed. Addon messages (DBM, Carbonite...) no longer command bots or open trades (SquidBots Lite is
  not affected).
- **Dispels**: a member stunned, feared, polymorphed or silenced is freed first, healers then tanks; one dispeller
  per member; cleanses laid on the ground or around the caster now reach the group, and spells that also strike
  enemies still cleanse (`AiPlayerbot.CoaSmartDispel`).
- **Marked allies**: bots step out of the reach of a member carrying a bomb, brand or beacon, read from the spell
  data, and the carrier steps away from the others (`AiPlayerbot.CoaAvoidMarkedAlly`).
- **Melee hybrids** close in instead of hardcasting from 20 yards; only their instants on the way
  (`AiPlayerbot.CoaMeleeNoRangedHardcast`).
- **Full bags**: random bots no longer leave a quest item or an uncommon or better drop on the corpse, the cheapest
  grey item makes room for it; your own bots' bags are not touched (`AiPlayerbot.CoaLootMakeRoom`).
- **Arenas**: the PvP trinket breaks a stun, fear, polymorph or charm of 2 seconds or more, in arenas, battlegrounds
  and duels (`AiPlayerbot.CoaPvpTrinket`); purgers take magic buffs off the enemy players (`AiPlayerbot.CoaArenaPurge`).

## v2.2.1 — Rated arenas with your party (October 2026)

Needs the core with the rated solo arena changes (jealous-sound/azerothcore-wotlk-coa, rated solo queue #7408 and
its follow-up: "Join as Group" in the rated solo system, Arena Points and runes per win, rating titles, season cap).

### Arenas
- **"Join as Group" in the Rated tab** with your bots: a party of 2 or 3 now queues in the rated solo system, and bots
  complete your side or play against you (jealous-sound/azerothcore-wotlk-coa#7562).
- A rated solo party still waiting gets a bot of the other role, as solo players do.
- A bot invited by its master to an arena team leaves its own solo queue team, or a bot team it does not lead, and
  accepts.
- The needs of the rated solo queues are logged (bots wanted per bracket and role).

## v2.2 — Rated solo queues, smarter group play and your reports (October 2026)

### Rotations — from the specs site reports
- **Primalist Mountain King**: Geode Barrage only on the Aftershock proc (instant), no more channelled filler.
- **Primalist Grovekeeper**: the Aftershock proc is spent on Geode Barrage at once (thanks Yeh).
- **Ranger Archery**: Uncover Weakness with 5 stacks of Advantage, right after Serrated Shot.
- **Bloodmage Fleshweaver**: no more Scarlet Delirium spam (an area fear, cast even with no enemy around).
- **Witch Doctor Voodoo**: Spirit Idol when mana runs low.
- **Starcaller Moon Guard**: no Stellar Drift in the tank rotation (it knocked packs away and pulled more).
- **Witch Doctor** (all three specs): no more cycling through wards and idols with no cooldown, each one replacing
  the last (Spirit Idol included) and draining the mana. The niche idols and Graven Effigy leave the rotation,
  Serpent Ward (damage specs) or Healing Ward (Brewing) only when it is not standing, Spirit Idol on low mana.
- **Starcaller Moon Guard** (tank): grabs packs in melee with Starsweep and Starburst instead of opening with a bow
  shot, Warden's Blade then Lunar Lance on a lost target, Celestial Strike first under 40% mana.
- **Cultist Heretic**: Tentacle of C'Thun is back while healing (only when it is not standing), Forbidden Ritual on
  group damage.
- **Necromancer Animation**: rotation and talent build of an experienced player: Harvest Plague, the three
  animations, Graveyard, Unholy Frenzy, Command: Undead, Death's Due, Blight left to burst and Lichfrost on a
  blighted target; no March of the Dead on a single target, no other raises next to the six Skeletal Warriors.
- **Pyromancer Flameweaving** (healer): no more Dragon Leap or Flame Step on cooldown into unpulled packs, and
  Kindle heals the group when mana runs low (PR #27 by ladiabale, from headless Deadmines runs; thanks!).
- Shipped as `zz_coa_rotation_corrections_2_1_1.sql` and `zz_coa_rotation_corrections_2_1_1_b.sql` (default
  rotations only, applied once at server start).

### Arenas
- **Rated solo queues**: bots join the rated 1v1, 2v2 and 3v3 solo queues when a real player is waiting, in the role
  the match still needs (`AiPlayerbot.CoaRatedSoloQueue`, `AiPlayerbot.CoaRatedSoloRoleFallback`).
- No bot 5v5 arena teams any more (Conquest of Azeroth has no 5v5); bot teams of a format set to 0 are disbanded at start.
- Teammates from the other faction (rated solo queues mix them) now show as friends for the match, not as enemies;
  their own faction comes back when they leave.
- Random bots back from an arena or battleground leave a party they were left in with a real player.

### Combat, arenas and group play
Each behaviour change has its own switch in `playerbots.conf` (all on by default, `0` turns one off).
- **Stability**: the command server listens on 127.0.0.1 only, bounds its input and runs commands on the world
  thread (`AiPlayerbot.CommandServerBind`); no crash when a bot flees from a player; no double delete of a ground-targeted spell cast while moving; items no longer read after
  being freed; locks on the bot event cache, the packet
  queue, the trainer and item caches, and chat heard in the bot's own update.
- **Area effects**: the detectors of ground and aura damage no longer give up on the first unrelated mob or aura. Tanks ignore passive summons that attack nobody.
- **Healers**: walk back into line of sight of whoever needs healing (`CoaRegainLineOfSight`); keep healing
  on the move (`CoaNoFacingWhileMoving`); stay in their combat strategies while the fight goes on
  (`CoaKeepCombatEngine`); heal themselves first when hurt and nobody is in danger (`CoaHealSelfFirst`); use their fast heals from 45% health when saving mana (`CoaFastHealBelowLow`).
- **Targeting**: next target in the same update and a wider facing (`CoaNextTargetSameTick`); raid
  exclusions also apply to marks and area attacks (`CoaExclusionsEverywhere`); sticky
  targets (`CoaStickyTarget`); mobs running for help are killed and snared first in dungeons
  (`CoaFocusRunners`); a target out of sight for a moment is kept (`CoaKeepTargetOutOfSight`).
- **Movement**: no more body pulls by walking past an idle pack in dungeons (`CoaBodyPullGuard`); no
  chasing an enemy far from the tank's fight (`CoaChaseLeash`); ranged and healers behind the tank,
  melee on its side (`CoaHoldBehindTank`).
- **Dungeon finder**: random damage dealers hold back only while a queued party really lacks a tank or a healer
  (`CoaLfgMissingRoles`).

### Fixes
- **Interrupts**: no kick wasted on a caster immune to its interrupt, even when the kick also deals damage (most
  classic dungeon bosses; issue #28 by ladiabale).
- **Pyromancer Draconic** bots now take Draconic Aspect: the spec's passive of the same name counted as the form, so
  they never cast it (issue #29 by mightyrathalos).
- **Worldforged chests**: bots no longer duplicate their loot by opening them again and again (squidbots-addon#2 by
  Dogsmeller), and no longer run back and forth to a pickup they have already looted (reported by Maky).
- **Full bags**: random bots whose bags are full now also get rid of gear they could wear but that is worse than
  their own (level 60 bots kept every soulbound raid epic they looted).
- **Custom races** (custom races module of the CoA Server Manager): random bots only take them when
  `CoACustomRaces.BotsEnable = 1`, skip the ids listed in `AiPlayerbot.ExcludedBotRaces`, and never take the races that
  crash the client (`AiPlayerbot.CrashingBotRaces`, default 65,72,77,80,81); Gnoll and Saberon bots are male.

### Gathering
- Bots with herbalism, mining or skinning only gather what still raises their skill (green or better for their
  client): no more bags of Peacebloom and Copper at level 60, and two alts with the same profession no longer fight
  over the same nodes (idea by Maky). `AiPlayerbot.GatherOnlySkillUp` (default 1); `nc +gather all` lets one bot
  gather everything again.

### Gear
- **Witch Hunter Houndmaster** no longer takes spell power gear: Agility, Haste and Crit, Spell Penetration first.
- **Barbarian Brutality** wears a two-hander and values Strength over Agility; **Runemaster Glyphic** wears a
  two-hander (its engravings need one).

## v2.1 — Arenas, alts and dungeon fixes (October 2026) — release main-20261012-e91c0624

Core: jealous-sound `main` of 12 October 2026 (`d5ff1cfa`) + two CoA Bots fixes (crashes under a heavy bot load,
Icecrown summon leak). Bots module: branch `coa-2.1`, tag `v2.1`.

### Arenas — 1v1, ready pylon, smarter bots
- **1v1 skirmish** from the 1v1 row of the PvP frame: queue solo and a bot comes to fight you (core #7269).
- **Rated solo queues** from the core (jealous-sound #7408): rated 1v1, and rated 2v2/3v3 matched by role (healer or
  damage). Players fill them for now; bots joining them comes next.
- **Ready pylon** in every arena starting room. Click it when you're ready; bots get ready once geared and buffed.
  When everyone is ready the gates open in 5 seconds, with a 5-4-3-2-1 countdown (`AiPlayerbot.CoaArenaReady`).
- Bots **prepare** before the gates: buffs, pets, and a spot by role — healers at the back, ranged in the middle,
  melee in front (`CoaArenaPreparation`, `CoaArenaRoleSpot`).
- Ranged and healers **kite** melee (`CoaArenaKite`), step back into **line of sight** instead of running at their
  target (`CoaArenaLineOfSight`), and use **pillars** under pressure (`CoaArenaCover`, measured in game for Nagrand,
  Blade's Edge and Ruins of Lordaeron).
- DPS **focus the enemy healer**, melee **peel** for their own, and a bot about to win **stays on its kill**
  (`CoaArenaFocus`, `CoaArenaPeel`, `CoaArenaPressure`).
- Duels: opening positions during the countdown (`CoaDuelOpening`). PvP: crowd control, then disengage and bandage
  when safe (`CoaPvpRecovery`, `CoaPvpBandage`).
- Fixes: arenas no longer end with everyone alive (some CoA forms counted as dead, core #7270); no more 2v3 (a bot
  stuck in combat outside missed the port); a combatant dead in the starting room is raised before the gates open.

### Your alts — with SquidBots Lite 2.1
- New chat commands read by the addon's bot sheet:
  - `mount ?` lists a bot's mounts, `mount <spell>` sets its preferred ground or flying mount (no restart any more),
    `mount clear` goes back to the automatic choice.
  - `talents ?` lists a CoA bot's talents and abilities with the points spent and left.
  - `talents options` / `talents learn <talent>`: choose **your own alts'** CoA talents rank by rank, under the
    talent frame's rules (level, prerequisites, active specialization, points of the level). Random bots keep their
    community build; `talents autopick` sets it again.

### Dungeons — from ladiabale's headless test runs
- Healer bots never cast a spell that moves them (no more Dragon Leap into unpulled packs).
- Bots step out of a point blank spin of **any** nearby enemy (Bladestorm), not only their own target's.
- Tank bots no longer chase a mob running away or for help.
- Bot-only groups now avoid ground effects too (Ramstein's Venom Belch): it only worked with a real player before.
- Interrupts: no kick wasted on a caster immune to it (most classic dungeon bosses), bots turn to the caster before
  kicking, a kick that needs a form the bot is not in is skipped (Ranger Toxic Dart out of stealth), and a kick with
  its own cast time is not started at a cast that ends sooner.
- A corpse one bot goes to resurrect is left to it (no more four rez casts on the tank while a dps stays down).
- A ranged damage dealer no longer leaps forward into the fight (Venomancer Skitter).

### Rotations
- **Primalist Grovekeeper** rebuilt from 12 real players' combat logs (thanks Mighty_Rathalos for the report): Seismic Wave and Earthquake on rage first,
  Wildclaw core, Geode Barrage only as a filler, Stoneshard removed.

### Fixes
- **Server slowing down over hours** (world tick up to 0.8 s mean after a day): Argent Champions in Icecrown piled up
  hundreds of summons that kept fighting with nobody around once a bot had passed by. Fixed in the core (PR #7353 sent
  to jealous-sound); the bench now stays around 20 ms.
- Random bots keep the configured number of battlegrounds going (`RandomBotAutoJoinBG<type>Count`), not one more per
  open battle: a realm set to one Alterac Valley ran four at once.
- No more "Hello" / "Hi" whispers when a bot joins your group, logs in, accepts an invite or comes by meeting stone
  (`AiPlayerbot.EnableGreet`, 0 by default).
- `lfg bot`: the first invite to a bot re-rolled to your level is no longer lost.
- Security: `.playerbots account link/unlink` checks the account name before using it.

### Dashboard 2.1
- "Turn the journals on" now writes the repack's `Settings\worldserver.conf.template`, so battleground statistics stay
  on after a restart.
- With the database down, the Armory and the other pages say so instead of showing a MySQL password warning.

### SquidBots Lite 2.1
- New **Alts** button and **My alts** panel: log your alts in or out as bots, summon them, see level, class, spec and
  XP; Teams (log a saved group in at once) and linked Accounts.
- **Bot sheet** for every bot: gear with the bot in 3D and its item level, bags and bank (give, equip, use, sell, drag
  and drop), quests (abandon), talents (see them, choose your alts' talents), favourite mount.
- Mounts and talents need CoA Bots 2.1. Thanks to Maky for the ideas.

Thanks to ladiabale for the dungeon reports, MinecraftParker, Mighty_Rathalos and everyone who reported on Discord.

## v2.0 — Tanks that hold, dungeons played like players (October 2026) — release main-20261011-a543922b
Core and bots are updated together. `CoA-Update-2.0\Update-2.0.bat` takes a **CoA Bots 1.9.1** install
(`main-20261007-b46a130e`: the 1.9.1 update or the full 1.9.1 repack), a **CoA Bots 1.9** install
(`main-20261007-ba8ce643`), a **CoA Bots 1.8** install (`main-20261004-b5f1c026`) or the **CoA repack of 30
September** (`main-20260930-df4dea11`, with CoA Bots 1.6, 1.7 or no bots). The full CoA Repack Bots 2.0 is for a new
install or a move from an older repack. On the CoA Server Manager, CoA Bots 2.0 comes with the `v2.0` tag.

The core is Jealous-Sound's main of 11 October (`0a0a21d3`, the arena fixes of 1.9.1 now included) plus our fixes for
four crashes seen under a heavy bot load; the bot server and `Start_All_Server.bat` run the same core. Its new
database updates apply by themselves at the first start. **Nothing else to run** after the update: no
`Installer-Bots`, no purge. `MaxPlayerLevel = 60` stays required (1.9.1 already has it; from older installs the update
sets it, as in 1.9.1).

### Tanking and aggro
- **DPS and healer bots never taunt.** All 219 taunts of the game are recognised, including taunts inside other
  spells, spells swapped in by CoA, and ground-targeted ones; pets' taunts are off in dungeons
- **Threat buffs go on tanks only**
- **DPS bots hold back** at 90 % of the tank's threat (110 % at range) while the tank holds the target, and also when
  their threat climbs fast enough to pass the tank within 1.5 s; melee bots stop their white swings too. They let the
  tank settle for 3 s on a new target, and keep their area attacks until the whole pack is on the tank. New option
  `AiPlayerbot.CoaThreatHold` (90; 0 = off)
- **Pets** of DPS and healers leave their taunt to the tank in any group with a tank (they keep it when the bot is on
  its own)
- **Tanks wear every threat aura they know**, and use their damaging abilities first
- Tanks without a threat stance (Guardian, Venomancer) open with their threat abilities as soon as someone gets close
- Tanks keep their non-damaging taunts for adds that run off, and use their defensive cooldowns before their attacks
  when hurt
- Bots no longer waste their global cooldowns: no recast of a debuff still on the target, no purge or spellsteal on an
  enemy with nothing to take off, no recast of a timed group buff that replaces its rival (Greater Marks, Greater
  Devotions, Greater Rites)
- **Knight of Xoroth** tank: no longer stuck recasting Impcaller; **Felsworn Tyrant**, **Sun Cleric**, **Reaper**,
  **Primalist** tanks hold much better
- Option `AiPlayerbot.CoaTankThreatBonus`: extra threat for tank bots, in percent. Off (0) by default

### Dungeons and mythic+ — `AiPlayerbot.CoaDungeonPriorities` (on)
- **Interrupt priority**: heals, crowd control and summons are kicked first, along with about a hundred spells of the
  classic dungeon bosses
- Bots **purge magic buffs** and **soothe enrages** on bosses and elites
- DPS bots **kill first** the adds and totems that heal or protect their side (tanks keep the boss)
- 0 = as in 1.9.1
- **#6090** A released ghost walks back in by the dungeon entrance and revives
- **#5753** A tank bot ahead of the player holds the front between pulls
- Bots stay awake when a group member fights nearby

### Damage dealers
- **Pets assist**: summoned pets of CoA classes start in defensive and attack their master's target (they never pull on
  their own)
- A bot in a fight is no longer carried away from its enemy by its own spell (Boulder Dash, Warpdagger, Advance...)
- Rotations reworked from real players' logs for **Stormbringer Wind** (summons its Air Elemental), **Primalist
  Grovekeeper** (fights in melee), **Tinker Mechanics, Demolition**, **Ranger Farstrider, Archery**, **Knight of
  Xoroth Hellfire**, **Primalist Wildwalker**, **Runemaster Engravement, Riftblade**, **Starcaller Sentinel** and **Sun
  Cleric Valkyrie** (Sunslam now cast)
- Level 60 talents: damage picks for Guardian Inspiration, Tinker Mechanics and Demolition, Ranger Farstrider; **no bot
  build keeps a talent a player could not reach** (all 70 specializations)
- Bloodmage Eternal stays set aside (`AiPlayerbot.CoaExcludedSpecializations = 99`): tried again, still broken

### Healers
- **Cultist Heretic**: its two tentacles no longer replace each other (a mana sink with no healing); Void Shield on the
  most hurt and the tank
- **Chronomancer Time**: Fabric of Time and Epoch first, Infinite Shield on the tank, no more Reverse Wound spam
- **Tinker Invention**: Zap!, Nanobot Reconstruction and its beacon before the slow Repair Shot; keeps Stim
  Augmentation
- Fewer deaths on the heal bench for all three (Heretic 4.4 to 0.7 per fight, Invention 3.0 to 1.0, Time 2.0 to 1.8)

### Raids
- CoA boss spells for **Molten Core, Blackwing Lair, Onyxia and AQ20** (PR #24 by alexanderschwarzkopf8)
- Blackwing Lair strategies for **Broodlord Lashlayer, the Black Drakes and Nefarian** (from mod-playerbots #2736)
- **Majordomo Executus** is fought

### Gear
- `AiPlayerbot.CoaGearMatchGroup` (on): a bot grouped with a player is geared at the player's item level
- `AiPlayerbot.CoaGearHighRiskPvp` (on): High Risk bots wear their PvP gear all the time
- **Season 1 honor and arena gear** recognised as PvP gear
- Bots keep their PvP gear for 10 minutes after a battleground
- Bots roll greed on shirts and tabards (#6895) and on the leveling legendaries of their own class
- Bots leave fireworks and common consumables alone
- **#5509** A tank that does not shoot no longer takes a bow with no stats

### Bot repair at login (no purge needed)
Your bots, their arena teams, PvP ranks and achievements are kept.
- Bots above the server's `MaxPlayerLevel` are rebuilt at the cap
- Once per bot: old bots drop their piled-up companions and extra mounts, and are geared again if they wear gear above
  their level
- **CoA Character Advancement** is set to the build of the bot's level at login
- Free talent points are picked

### City life
- Some free random bots visit their faction's capital for 10 to 30 minutes: bank, auction house, inn, trainers,
  vendors. Options `AiPlayerbot.CoaCityStayMinMinutes` / `CoaCityStayMaxMinutes`; how often:
  `AiPlayerbot.RpgStatusProbWeight.GoCity`

### Looking for group
- `lfg bot`: a bot recruited for a player at level 15 or more is never under 15 (the dungeon finder refused the group),
  and at the server's level cap it is at the player's level (end-game dungeons); in between the level spread is kept

### Fixes
- **Crash on some chat commands** fixed (already in 1.9.2 for the CoA Server Manager)
- Auto pull distance is measured from the tank, or from the player
- No more "Hello" and "AI was reset to defaults" whispers when bots join you (`AiPlayerbot.EnableGreet`, now 0 by
  default even when the line is missing)
- `AiPlayerbot.DeleteRandomBotAccounts = 1` also clears the CoA character tables, so new bots inherit nothing from old
  ones (set it back to 0 after one start)

### Dashboard 2.0 — Armory
- New **Armory** page: search a character or a bot, see its gear with icons and tooltips, item level, stats,
  professions and achievements. Works on a CoA Server Manager server too

### SquidBots Lite 2.0
- `/sbl scale` and Ctrl + mouse wheel set the bar's size; `/sbl show` and `/sbl hide`; saved per character

### Settings
- The update adds the new options to your `playerbots.conf`; `CoaThreatHold` moves from 0 (1.9.1) to 90 unless you
  had changed it
- For server tools: `playerbots.conf.settings.json` (read by the CoA Server Manager) lists the new options
  `CoaGearMatchGroup`, `CoaGearHighRiskPvp`, `CoaThreatHold`, `CoaTankThreatBonus`, `CoaDungeonPriorities` and the
  city stay options

## v1.9.1 — Gear for the activity, arenas from the PvP frame (7 October 2026) — release main-20261007-b46a130e
Core and bots are updated together. `CoA-Update-1.9.1\Update-1.9.1.bat` takes a **CoA Bots 1.9** install
(`main-20261007-ba8ce643`: the 1.9 update or the full 1.9 repack), a **CoA Bots 1.8** install
(`main-20261004-b5f1c026`) or the **CoA repack of 30 September** (`main-20260930-df4dea11`, with CoA Bots 1.6, 1.7
or no bots). The full CoA Repack Bots 1.9.1 is for a new install or a move from an older repack. On the CoA Server
Manager, CoA Bots 1.9.1 comes with the `v1.9.1` tag. The core is the 1.9 core plus the arena queue fix
(Jealous-Sound PR #6847); the bot server and `Start_All_Server.bat` run the same core. SquidBots Lite 1.9, the
Dashboard 1.9 and the rotations are unchanged.

**`MaxPlayerLevel = 60` is now required.** With 80 (the core's default), the game client takes 80 for the level cap
and greys the whole arena out. The update sets 60 in `Settings\worldserver.conf.template`, once: the only repack
setting it touches, said on screen and written to `Backups\1.9.1-<date>\settings-before\settings-changed.txt`; the
template from before is in the backup and the undo puts it back. `CoA-Bots\Start_All_Bots.bat` also forces 60 in
the `worldserver.conf` it builds at each start, and the full repack ships 60. Do not set it back to 80.

### Gear for the activity — `AiPlayerbot.CoaGearByContent` (on)
- Random bots put on **PvP gear** in battlegrounds, arenas and their queues, and all the time when they play in
  **High Risk**; they go back to their **PvE gear** as soon as they leave the battleground or the arena
- **Grouped with a player**, a bot gears at the **player's item level** (the average of the pieces he wears): a
  player at item level 62 gets bots around 55-62. In PvP, never below the first honor tier (item level 66)
- A level 60 bot alone in PvP takes the honor tier of its best arena rating (Knight-Lieutenant's to Field
  Marshal's); a bot with no team keeps a fixed tier of its own
- PvP pieces are those of the honor quartermasters (475 pieces) and those with resilience
- Only the bots of the random bot accounts, never the players' characters. One regear per second on the server, bots
  grouped with a player first
- A regeared bot is never left without gear: grouped with a low item level player, the limit never goes under the
  bot's level + 5, and a slot left empty is filled anyway
- Pieces that scale with their wearer (leveling legendaries included) count at the player's level when the
  player's item level is measured
- Added to your `playerbots.conf` by the update. 0 = gear as before

### Arenas from the PvP frame (core, PR #6847)
- **Quick Match 2v2 / 3v3** (skirmish): queueing works from the PvP frame, no battlemaster needed; bots fill the
  teams. Tested: full games in 2v2 alone and with a bot
- **Rated "Join Solo"**: queues as a skirmish for now (no rating, no arena points)
- The 1v1 row of the PvP frame no longer queues a 5v5 game.
- Not yet: 1v1 games and a real rated solo queue

### Login message
- The first line now points to the SquidBots addon (https://github.com/Zyth45/squidbots-addon) for easy bot control;
  the CoA Bots line keeps the source link and credits mod-playerbots.

### Fixes
- **No more taunts from damage dealers and healers** (reported by Dogsmeller): no taunt at all outside tanks, even
  one that deals damage; checked on every spell cast, rotation lines included
- **Threat buffs for tanks only**: a damage dealer or a healer removes a threat buff it put on itself, and a threat
  buff only goes on a tank
- **Starting zones**: Blood Elf and Draenei bots left at Sunstrider Isle / Ammen Vale (made before CoA moved their
  start to the Undead at Deathknell / the Night Elves at Teldrassil) go to their race's starting point at login, and
  do the quests of those zones
- **#6261** A bot floating above the flight master after a flight comes back down
- **Added races** (Worgen, Vrykul… on realms that have them): their faction comes from the core's race table; they
  all counted as Horde (idea of rrawnsley, his PR #17 on our mod-playerbots, rewritten)
- **Stuck low-level bots**: a bot of level 10 or lower, alone, that earns no XP for 10 minutes moves to another place
  of its level
- **#6724** Templar Oathkeeper: Shackle the Unrepentant (it makes the target immune) is no longer cast on the target
  being hit
- **#6704** KoX Defiance, and every shield spec: no more two-handed weapons
- **#6505 / #6493** Tank: leads the group only while the player moves on, and stays put when the player stops (no
  more back and forth in Razorfen Kraul)
- **#6802** A bot standing still indoors (an inn) gets off its mount

## v1.9 — PvP (7 October 2026) — release main-20261007-ba8ce643
Core and bots are updated together. `CoA-Update-1.9\Update-1.9.bat` takes either a **CoA Bots 1.8** install
(`main-20261004-b5f1c026`: the 1.8 update or the full 1.8 repack) or the **CoA repack of 30 September**
(`main-20260930-df4dea11`, with CoA Bots 1.6, 1.7 or no bots). The full CoA Repack Bots 1.9 is for a new install
or a move from an older repack. On the CoA Server Manager, CoA Bots 1.9 comes with the `v1.9` tag. The bot server
and `Start_All_Server.bat` run the same core.

1.9 is the PvP release: bots play rated arenas, can live in High Risk or War Mode, and play battlegrounds as a
team. **PvP is on by default**, on a new install and after the update:
- `AiPlayerbot.RandomBotAutoJoinBG = 1`: the bots keep one Warsong Gulch, one Arathi Basin, one rated 2v2 and one
  rated 3v3 going at level 60
- `AiPlayerbot.CoaRulesetForBots = 1`: 30 % of the random bots are in High Risk

Either one can be turned off: 0 in `playerbots.conf`, or on the Bots page of the CoA Server Manager. More bots in
battle cost some CPU: see "Battlegrounds" below for how to lower it.

### Rated arenas
- Random bots now make arena teams at the top level of the realm (60 on CoA). They used to wait for level 70, so
  CoA had no bot team at all
- Bot teams play rated 2v2 and 3v3 with their own ladder: ratings from 1000 to 2000, kept between restarts. A
  player team that queues for a rated arena finds bot opponents at any rating
- The captains of the bot teams stay online, and are logged in right after a restart. A team only queues when its
  captain is there, and each restart used to log in other bots: the teams now play again within minutes of a
  restart. 90 rated fights in our test night
- The bots also keep one rated 2v2 and one rated 3v3 going between themselves (`RandomBotAutoJoinBG = 1`, and
  `AiPlayerbot.RandomBotAutoJoinBGRatedArena2v2Count` / `3v3Count`: 1, before 0; 5v5: 0). Teams per format:
  `AiPlayerbot.RandomBotArenaTeam2v2Count` / `3v3Count` / `5v5Count` (10 / 10 / 5)

### High Risk and War Mode — `AiPlayerbot.CoaRulesetForBots` (on)
- 30 % of the random bots play in High Risk (`CoaRulesetHighRiskPct`, 30) and none in War Mode
  (`CoaRulesetWarModePct`, 0); the rest stays in PvE. A bot always gets the same ruleset. 0 = every bot in PvE, as
  before
- High Risk and War Mode bots keep the PvP flag on. The core only put the aura, so on a PvE realm nobody was ever
  flagged by it
- As on CoA, any bot, a PvE one too, may attack a High Risk or War Mode character, and nobody starts a fight with a
  PvE one. A bot still defends itself and helps its group. Bots never lose items
- In dungeons and raids the ruleset changes nothing, and grouping with such a bot does not change yours
- Read again by `.reload config`

### Battlegrounds
- **Balanced sides**: bots of every level of the bracket still join, but both sides keep about the same average
  level. On a test realm the sides started 4.5 to 11 levels apart, and the higher side won nearly every game; now
  about 2 levels on average. A bot refused for the balance waits a minute before it tries that queue again.
  `AiPlayerbot.CoaBgLevelBalanceChance` (15 %; 100 = as before). Queues a real player waits in are filled as before
- **Bot battlegrounds by default**: `RandomBotAutoJoinBG = 1` keeps one Warsong Gulch and one Arathi Basin going at
  level 60 (`AiPlayerbot.RandomBotAutoJoinBGWSCount` / `ABCount`: 1). 0 = bots only fill the queues of players
- **They start on time**: the bots queue once 95 % of them are logged in, not all of them. On a realm of 1,000 bots
  no battleground started for 30 to 40 minutes of every hour
- **CPU**: a bot in a battleground or an arena works harder than one questing, and these games keep about 60 bots in
  battle at all times. On a small PC, set `WSCount`, `ABCount` or the rated arena counts to 0 one by one, or
  `RandomBotAutoJoinBG = 0` (bots then only join the games players queue for)
- **Warsong Gulch**: the flag carrier is escorted, healers heal it first, and the enemy carrier is hunted down. 24
  games in 25 now end at 3 captures (16 in 21 before)
- **Arathi Basin**: a base under attack is defended and taken back by the bots nearby, and defenders spread over
  the bases. About twice as many bases defended per game
- Tank-follow and catch-up moves stay out of battlegrounds: a flag carrier no longer turns back to its group
- **Stuck bots**: a bot standing still for 30 s out of combat, far from its objective, takes a few steps; still
  stuck after 90 s, it starts again from its side's graveyard. This also works for a bot busy with something else.
  Test night: the longest stuck bot waited 96 s, and none at the spots known before
- A rotation line that summons and brings no summon within 10 s is set aside for 30 s instead of blocking the bot
  (a Knight of Xoroth stood still 170 s)

### Stability
- **Server crash with many bots fixed** (core): with 1,000 bots or more, mostly new ones grouped in starting zones,
  the server could stop dead after 5 to 80 minutes (code 0xC0000409, no crash file). A line-of-sight test of the
  core ran past its memory on some rays; it now stops cleanly. Proposed to Jealous-Sound (#6687). No crash in our
  test night
- The fixes of 1.8 for crashes under a heavy bot load are kept

### Classes and rotations
- Rotations follow Jealous-Sound's latest class fixes: **Bloodmage** (Lunge for Accursed, Blood Veil, Infuse for
  Fleshweaver), **Witch Hunter** (Trap Launcher) and **Barbarian** Ancestry (Breath of The North). Bloodmage
  Sanguine +7 %, Witch Hunter Inquisition up from tier C to B, the others stable
- Witch Hunter Black Knight keeps its 1.8 Trap Launcher line: the new one made the tank step back
- **Cultist Godblade**, from a player's tip: Insanity is built up and Entropic Slam is spent during Total Madness,
  when it costs nothing. Total Madness about 4 times as often (38 % of the time instead of 10 %); boss damage
  x1.16 at our bench, not yet a sure gain. New rotation trigger for such lines: `aoe has aura::<aura>`
- Witch Hunter Houndmaster takes Pure Agony, Darkhunter and Coordinated Killing; Chronomancer Infinite takes Pure
  Chaos (Time Loop during Incarnation of Chaos). Both from players' advice, and in the builds of top players
- `talents spec tank|heal|dps|random` never picks a specialization the bots never play: a Bloodmage asked to tank
  no longer becomes Eternal. A specialization asked by name is still given
- The default rotations of the 71 specs ship in one file, applied once by the installer; your bots' own lines are
  not touched

### Groups, settings and comfort
- **Bot chat language** (no AI involved): `AiPlayerbot.BotTextLocale` (-1 = the language of the players' game
  client, 0 English, 2 French, 3 German, 6 Spanish, 8 Russian). Nearly every line exists in French and German,
  about half in Spanish and Russian. Now a list of choices in the CoA Server Manager and in the Dashboard. Read when
  the server starts
- Random bots leave a group whose players stay offline: once none of its players has been connected for
  `AiPlayerbot.CoaOrphanGroupReleaseDelay` seconds (300; 0 = as before), they go back to their own life. A player
  who comes back in time keeps them; battleground, arena and dungeon finder groups and your own bots are never
  touched
- Bots no longer whisper "Equipping [item]" for each upgrade they put on by themselves (a raid of 25 bots filled the
  chat): `AiPlayerbot.CoaTellEquipUpgrades` (0; 1 = as before). An item you ask for is always answered
- With `AiPlayerbot.Enabled = 0` (servers of the CoA Server Manager that run its Companions instead), our "lfg bot"
  recruiting, the status file and the bot level manager stay quiet
- The installer says when a big SQL import is going to take a few minutes

### CoA Server Manager (Corfirean)
- Compatible (tested on 6 October). The Manager takes CoA Bots 1.9 from the `v1.9` tag: update through the Manager,
  not with these zips
- Its Bots page now has 50 settings: the rated arena fights and the bot teams for each format, High Risk / War Mode
  on or off and their shares, and the bot chat language as a list of choices. PvP can be turned off there too

### SquidBots Lite 1.9 and Dashboard 1.9
- **SquidBots Lite 1.9: Specializations**. Ask a bot for a specialization from its party frame menu (Tank, Healer or
  Damage in one click, or any of its specializations), or the whole group from **Bots > Specializations**
  (`/sbl spec`): **Balance the group** gives one tank, one healer and everyone else on damage. No GM rights needed,
  and it works for your alts. Idea from **Ard**
- **Dashboard 1.9**: a **PvP** tab (battlegrounds, arenas, top arena teams, High Risk and War Mode), and the bot chat
  language on Settings and Chat & Loot

### Core: Jealous-Sound main of 6 October (57 commits after the 1.8 core)
- Charges of Hero and stock-family spells, without their category cooldown, for every class (#6608)
- Bloodmage abilities and spell patches (#6526), abilities under Sanguine Essence and Transgression (#6577),
  Heartbreak (#6622) and more; Pyromancer (#6623, #6624); Sun Cleric Sun Down (#6644); Necromancer Corpse Explosion
  (#6646); Tinker mines and bombs hit neutral targets (#6679); Ranger Falconstrike weapon percentages (#6499);
  class fixes batch 16 (#6496)
- Heroic and Mythic for the 19 vanilla dungeons (#6477), their melee damage set from Ascension's logs (#6561),
  Dungeon Spoils for random Heroic and Mythic runs (#6671), released players revived at the nearest dungeon start
  (#5255)
- Ascension's creature families and taming for Heroes (#6380), equipped enemy gear drops (#6434), Three Totems in
  Mulgore (#6514)
- Two SquidBots commits of 1.8 are now in Jealous-Sound's core: collections of alts played as bots (#6399), and your
  own requests left to you while your alts play as bots (#6519)
- With the two SquidBots fixes: crashes under a heavy bot load (as in 1.8) and the line-of-sight crash (#6687).
  Build `ba8ce643`
- The core SQL updates apply by themselves at the first start (5 to 15 minutes, once)

### Installing and updating
- **One update for two starting points**: CoA Bots 1.8 (its update or its full repack) and the repack of 30/09 (CoA
  Bots 1.6, 1.7 or none: it offers to install the bots). Older repacks are refused before anything is written:
  take the full repack and `Migrate-Accounts`
- Same steps and the same safety as in 1.8: checks first (nothing written while one fails), full backup in
  `Backups\1.9-<date>`, new files, settings merged, bot database and 1.9 rotations, addon, then a test start
- **Settings merged**: your values are kept, missing settings are added with their comment, and a value still at a
  default we shipped (1.4 to 1.8) moves to the 1.9 one (`RandomBotAutoJoinBG` and the rated arena counts go from 0
  to 1; `CoaRulesetForBots` is added at 1). List:
  `Backups\1.9-<date>\settings-before\settings-changed.txt`
- **Undo**: `Backups\1.9-<date>\Retour-Arriere.bat` (or `Update-1.9.bat rollback`). A cut update is finished or
  undone by running `Update-1.9.bat` again; `Update-1.9.bat status` tells where it is
- Already on 1.9 but a file was changed: `Update-1.9.bat --repair` lays the 1.9 files again
- No purge needed: the bot arena teams are made at the first start

### Known issues
- Bloodmage Eternal (tank) stays out of the bots: the core still does not give its form (tested again on 6 October)
- Tinker: Overcharge on a beacon can crash the server (Jealous-Sound's core; reported, waiting for a crash file)
- Alterac Valley can lean to one side, depending on the classes drawn (Witch Hunters, Necromancers). Spreading the
  strong classes over both sides is an idea for later
- Between bot teams, rated fights are capped at about 18 an hour: the core only matches teams close in rating
- `Start_All_Server.bat` does not know about a cut update: run `Update-1.9.bat` again first
  (`Start_All_Bots.bat` refuses on its own)
- Moving from the all-in-one repack of 15 September has not been tested

Thanks to **Jealous-Sound** for the core, to **Corfirean** for the CoA Server Manager and the crash fixes our core
commit builds on, to **Ard** for the Specializations idea, and to the players who sent their tips.

## v1.8 — Full Strength (4 October 2026) — the CoA repack of 30/09 brought to the 1.8 core
Core and bots are updated together: `CoA-Update-1.8\Update-1.8.bat` on the repack of 30 September
(`main-20260930-df4dea11`, with CoA Bots 1.6, 1.7 or no bots), or the full CoA Repack Bots 1.8 for a new
install or a move from an older repack. The bot server and `Start_All_Server.bat` run the same core.

At our boss bench, the median damage of one bot over the same 42 damage specs went from 685/s (1.6, 30/09)
to 986/s (1.7) and 2,342/s (1.8). Most of the jump comes from the passives fix below (a 1.7 bug: many bots
fought without their passives), the rest from the rotations, the talents and the core.

### Bots keep their passives — `AiPlayerbot.CoaKeepPassiveAuras` (on)
- A rebuilt bot (randomize timer, level change, `init=` commands) lost every aura, **its ~41
  passives and PvE Mode included**, until its next login. Such a bot did **0.42x** the damage of the same bot
  with its passives; 57 % of the bots measured were hit, in every spec (x1.2 Grovekeeper to x5.5 Infernal).
  A Sanguine without Thirst never cast Valanar's Vengeance
- Passives and rule auras now stay; buffs, debuffs and procs go in one pass over a fixed list, as before.
  Team damage at the boss bench: 14,082 -> 22,884/s and 9,523 -> 22,260/s on our two test servers
- Bots that lost their passives in 1.7 get them back at their next login: no purge needed
- Read again by `.reload config`; 0 = as in 1.7
- Barbarian: Ancestral Combat is laid at the start of every fight (the core gave it in 43 % of fights,
  Ancestry lost about a third of its damage without it) — `AiPlayerbot.Coa.AncestralCombatFix` (on)

### Held channels — `AiPlayerbot.CoaKeepChannels` (on)
- From a report by **Yeh** (Maelstrom). Attack channels are held to the end (Stormflow, Aortic Assault,
  Twilight Frenzy, Carve): the next attack waits instead of breaking them. Spells the class may cast during
  the channel without breaking it are allowed (Brine and Electrocute during Stormflow)
- Heals, defensives, taunts, interrupts and movement still break the channel. Never held: healing or
  defensive channels, any channel of a healer, of a bot at critical health, or longer than 15 seconds
- Read again by `.reload config`; 0 = as in 1.7

### Exclusive buffs — `AiPlayerbot.CoaExclusiveFamilies` (on)
- From the same report: Maelstrom bots recast both Pressures in a loop. For each family of exclusive buffs
  (Stormbringer Aegis and Pressures, Barbarian shouts, Starcaller Aspects...) a bot keeps the member its
  rotation ranks highest, never replaces one another caster put on, and buffs again at once a group member
  raised from the dead
- Running Wild is no longer handled as a stance
- Read again by `.reload config`; 0 = only the families of the core's aura scripts are kept to one

### Rotations — 31 specs get a better default rotation
Half of the bots play a candidate rotation, half the current one, in the same boss fights; the candidate
becomes the default only when it wins overall and in each group of bots. Gain = median damage, new / old.
- **3 October, morning (14 specs)**, measured before the passives fix (real gain a little lower): Felsworn
  Infernal x1.73-2.28, Ranger Archery x2.64, Runemaster Glyphic x1.72, Pyromancer Incineration x1.54-1.65,
  Witch Hunter Inquisition x1.38-1.61, Sun Cleric Piety x1.28-1.58, Pyromancer Draconic x1.41-1.57, Sun Cleric
  Valkyrie x1.22-1.55, Bloodmage Accursed x1.37-1.54, Guardian Gladiator x1.41-1.49, Reaper Harvest
  x1.27-1.41, Starcaller Sentinel x1.28-1.36, Ranger Brigand x1.34, Chronomancer Artificer x1.14-1.29
- **3 October, evening (11 specs)**: Chronomancer Infinite x1.59, Runemaster Engravement x1.57, Necromancer
  Animation x1.52, Primalist Wildwalker x1.40, Witch Hunter Houndmaster x1.40 (**Dogsmeller's tips**), Knight
  of Xoroth Hellfire x1.39, Barbarian Brutality x1.32, Barbarian Ancestry x1.27, Starcaller Warden x1.23,
  Necromancer Death x1.19, Guardian Inspiration x1.18
- **4 October, morning (6 specs)**: Templar Zealot x1.76 and Barbarian Headhunting x1.33
  (**StarTraderPaulus's tips**), Stormbringer Lightning x1.36, Bloodmage Sanguine x1.32, Venomancer Stalking
  x1.29, Runemaster Riftblade x1.11
- Still on the bench (no clear difference yet): ilusixn's tips for Moon Guard and Brutality, Yeh's Maelstrom
  rotation, StarTraderPaulus's tips for Knight of Xoroth War, Animation, Mountain King and Corruption. Tips
  that lost were taken out. Ranger Farstrider is left aside (Falconstrike, see Known issues)
- Shipped as `Database\playerbots\custom\zz_coa_rotation_corrections_1_8.sql`: default rotations only (the
  bots' own lines are not touched), checks itself, safe to run again

### Talents
- **Talents from top players**: 50 swaps over 18 classes (Barbarian, Bloodmage, Chronomancer, Cultist,
  Felsworn, Guardian, Primalist, Pyromancer, Ranger, Runemaster, Starcaller, Stormbringer, Sun Cleric,
  Templar, Tinker, Venomancer, Witch Doctor, Witch Hunter), free choices of the support specs included.
  Taken at login
- **Build table rebuilt from the game's data** (0 rule breaks instead of 776): talents follow the tree gates
  while levelling, no spec keeps points back, the **Black Knight gets Chains of Darkness at level 11**
  instead of 43
- A bot never asks the core for a talent rank the realm does not have: **Ancestry and Draconic bots created at
  level 60 get Ancestral Evolution and Timed Attacks** (before: rank refused, no talent at all)
- **Wind and Farstrider take their four free choices** (Clear Skies, Tailwind, Aerodynamics, Tempest's Call /
  Horn of War, Gutpiercer, Horn of Alacrity, Frenzy)
- Eight specs spend the one or two points they used to leave empty

### Gear
- Stat weights fixed for 14 classes or specs: Tinker (spell power and intellect; Demolition did 2-2.8x more
  damage in spell gear), Barbarian and Ranger Brigand (strength counts as attack power), Felsworn Infernal
  (spell power and hit first), Templar Zealot (agility and hit), Bloodmage Accursed, Chronomancer Artificer
  (the wand counts), Pyromancer (spell power before intellect), Cultist Godblade, Primalist Grovekeeper,
  melee Runemaster, Witch Hunter Inquisition, Knight of Xoroth Hellfire (no more healer plate before
  strength plate), Reaper Soul (strength first)
- Starcaller Sentinel and Moon Priest: the ranged slot takes a bow, no relic. 65 Sentinels in 73 held an idol
  and never cast Starfire Shot, Huntress Shot or Trueshot: Sentinel damage x2
- Level 60 bots in early raid gear on a fresh install too (`RandomGearQualityLimit = 4`,
  `RandomGearScoreLimit = 78`, `RandomGearLoweringChance = 0.3`); the old default 3 / 0 / 0 moves to these

### Classes
- Knight of Xoroth casts Greater Mark of Korth'azz at last: spell and aura names now compare letter by letter,
  whatever the case or the shape of the apostrophe
- Tinker Mechanics puts its Sentry Turret down even with the Mechsuit or a drone out (2 fights in 6 before,
  12 in 12 after); `coa summon missing::<creature>` looks at that creature only
- Ranger Archery takes Incendiary Shot instead of Brutal Shot, like the best players

### Groups, dungeons and bosses
- Out of the way of boss casts stays on (`AiPlayerbot.CoaBossKnowledge`), and a bot set to passive still
  steps out of a boss's area cast
- A bot tank no longer taunts what another tank holds; only the group's main tank takes a target back
- Healers no longer recast a heal over time already on the target (922 failed tries in 1,715 before), and no
  longer skip a tank without one
- A bot that went through Razorfen Kraul or Shadowfang Keep no longer keeps that dungeon's strategy everywhere
- `co -rtsc` and `nc -tell target` work (strategy names fixed)
- A bot looking at a scaled item no longer stops the server (Zyth45/mod-playerbots#16)

### Fixes from your reports (issues on Jealous-Sound's tracker)
- Chronomancer bots no longer cast Temporal Return between two packs: it sent a healer home in the middle of
  a dungeon (#6286)
- Ranger bots no longer cast Befriend Beast: the beast and the bot stood idle while it took hits (#6111)
- The attack kit no longer casts on the group's target a control that the first hit breaks: a Time
  Chronomancer cast Babify, which also heals, on the enemy being killed (#6287)
- Bots never charm or possess a boss: a Felsworn bot's Enslave Elemental broke the Noxxion fight (#4835);
  trash can still be charmed
- A Cultist bot no longer recasts Tentacle of Yogg-Saron all fight long while its own still stands (#6093)
- An equipping bot names the item in full, random suffix included ("Watcher's Cape of Spirit") (#6278)

### Options shipped off
- `AiPlayerbot.CoaShortLivedDebuffs` (0): 1 = `debuff missing` / `cast debuff` no longer ask 8 s of life
  left on the target, only half the debuff's duration (area spells: every enemy in the area counts). Tested
  on the bench against the 1.7 rule: no gain, so off. Read again by `.reload config`
- `AiPlayerbot.CoaStealthIsNotStance` (0): 1 = Ranger Elude, Venomancer Skulk, Reaper Underwalk and
  Runemaster Runeshroud are no longer worn out of combat as a stance (no slow, hidden walk in towns). Not
  measured alone yet, so off. Read once at startup

### Your alts as bots, new models for you
- `CoA.CollectionsForBots = 1`: your own characters played as bots (`.playerbots bot add`, or My alts in
  SquidBots Lite) wear their wardrobe, outfits, vanity items and mounts. Random bot accounts (prefix taken
  from `AiPlayerbot.RandomBotAccountPrefix`) load no collections. Proposed to the core as #6399
- With your alts online as bots, your own requests stay yours: wardrobe and outfits, vanity and store, talents,
  Manastorm, Wildcard picks, bug reports and GM tickets. An alt of the same account could take them before (they
  were lost, or applied to the alt), and its logout could erase them (core fix, from Jealous-Sound's review of #6399)
- `CoA.SendDisplayPatches = 1`: players get the custom creature models their client lacks; our core fix skips
  bot sessions, which received thousands of these packets each (#6400)
- Both are set in the bot server's `coa.conf` at every start when `Settings\coa.conf.template` does not set
  them (a value in the template wins). The bot server also gets `MaxCoreStuckTime = 600`: frozen for 10
  minutes, it stops with a crash file instead of hanging

### Core: Jealous-Sound main of 4 October (about 150 commits after the 30/09 repack)
- Login freeze fixed: item display rows streamed on demand (#6404)
- Molten Core restored (#6015), Basalthane encounter in Onyxia's Lair (#5750), Deadmines and Upper Blackrock
  Spire bosses with their Ascension abilities (#6282, #6175, #6062)
- Leveling legendaries (#6393, #6405); loot and quest rewards lifted to the level of scaled content (#6403)
- Class mechanics batches 1 to 11: Shadow Rage scales with Lethal Pursuit stacks and rolls on Rampaging
  Hounds cleave hits (#6350, #6383; at our bench the average Shadow Rage hit went from 220 to 424 and its
  share of the Houndmaster's damage from 10 % to 19 %), script-rolled procs no longer roll twice (#6317), Torrential Wrath costs 40 Static (#6333), charges, cast speed, Echo and hounds (#6217),
  Sanguine Mend echo (#6209), Brood Marks kept (#6231), Sacred Swing after a dodge or parry (#6159), and more
- Native talent builds, specialization slots and Tomes (#6244); Character Advancement for free-pick Heroes
  (#6358); one stat path at a time (#6367)
- Northshire revamp (#5764); Blood Elves start in Deathknell, Draenei in Shadowglen (#6397); 128 character
  slots (#6041); Glory for honorable killing blows and objectives (#6339); quest reward and group loot
  appearances (#6031, #6036); rest in inns (#5774)
- Scaled items lifted in five-level steps, with the patch rows and bytes sent to each player logged (#6419);
  and one pull request still open at Jealous-Sound, taken ahead: Guardian's Standards, Caltrops and the other
  class summons respect their owner across factions (#6420)
- With the SquidBots commits (listed in `RELEASE.json`): four crashes seen under a heavy bot load, the
  collections of alts played as bots, display patches skipped for bot sessions
- The core SQL updates apply by themselves at the first start (5 to 15 minutes, once)

### SquidBots Lite 1.8 and Dashboard 1.8
From now on the addon and the dashboard carry the version number of CoA Bots.
- SquidBots Lite 1.8: **Bots > My alts** (`/sbl alts`) — log in, summon or log out the other characters of
  your account as bots. Also carries 1.6 (Raid of 10 / 25 bots, Regear in GM mode)
- **Dashboard**: Notable finds, the chat feed and Most talkative work again with the bots' two-word names
  (since 1.7, bots carry a surname). Your `dashboard.json` (port) is kept on update

### Installing and updating
- **Update tool** `CoA-Update-1.8`: extract the zip into the repack, run `Update-1.8.bat`. Checks first
  (nothing written while one fails), full backup in `Backups\1.8-<date>` (database and files), new files,
  settings merged, bot database, addon (it asks), then a test start of your server
- **Settings merged**: your values are kept; settings missing from your `.conf` are added with their comment
  in a dated section at the end; a value still at a default we shipped (1.4 to 1.7) moves to the 1.8 one.
  List: `Backups\1.8-<date>\settings-before\settings-changed.txt`, with a copy of your files
- **Undo**: `Backups\1.8-<date>\Retour-Arriere.bat` (or `Update-1.8.bat rollback`). A cut update (window closed,
  power cut) is finished or undone by running `Update-1.8.bat` again; `Update-1.8.bat status` tells where it is
- Free disk space needed: about twice your database (`mysql\data`) plus 2 GB, so that the undo always works.
  A repack under Program Files, or in a path with accented letters or too long, is refused before anything
  is written
- Installer: the bot SQL is imported without `cmd /c` (it failed on some Windows versions and left an empty
  bot database), and stops at the first failure; a settings file saved as ANSI or UTF-16 is converted
- `coa.conf` and `worldserver.conf` of the bot server are rebuilt from `Settings\*.template` at every start;
  each copy now says so in its first line (change the template, not the copy)
- `Migrate-Accounts` checks it reads the other repack's database, even when both repacks use the same MySQL
  port; run `Installer-Bots.bat` after it
- The version check says which part differs: the repack release, or the `worldserver.exe` (with both
  fingerprints)
- `dashboard.json` is kept on update
- The 1.7 rotation file `zz_coa_rotation_corrections.sql` ships byte for byte as in 1.7: the server never runs
  it again, so it cannot rewrite your bots' own rotation lines (the 1.8 changes are in a separate file)

### Known issues
- Ranger Farstrider: Falconstrike rank 8 hits far too hard (core). Ranger Bushcraft items do not exist in
  game yet (core)
- Reported to the core: Chromatic Shard's instant proc is never given (Anomaly Spikes rank 2, #6249),
  Stabilizing Pressure stacks with the other Pressures (#6398)
- `Start_All_Server.bat` does not know about a cut update: run `Update-1.8.bat` again first
  (`Start_All_Bots.bat` refuses on its own)
- Moving from the all-in-one repack of 15 September has not been tested

Thanks to **Jealous-Sound** for the core, and to **Dogsmeller**, **Yeh**, **ilusixn** and **StarTraderPaulus**
for their tips.

## v1.7 — Ready to Raid — for repack main-20260930-df4dea11 (same repack as 1.6: only the CoA-Bots folder changes)
### Raids
- **A raid of bots in one command**: `.playerbots coa raid [size] [tanks] [healers]` — 25 = 3 tanks, 6 healers and
  15 damage dealers by default, 10 = 2 tanks and 3 healers; classes spread out, each bot shows its role in the raid
  panel. **Open to every player**: 5 raids of bots at a time on the realm, one raid every 30 minutes per player
  (`AiPlayerbot.CoaRaidPlayerMax`, `AiPlayerbot.CoaRaidPlayerCooldown`); game masters have no limit
- **Level 60 bots in early raid gear**: about 70% epic, the rest rare, up to item level 78 (dungeon epics, Tier 1,
  Molten Core, Onyxia, Zul'Gurub), Worldforged items included. GMs: `.playerbots coa regear` gears the bots online again
- SquidBots Lite 1.6: "Raid of 10 bots" and "Raid of 25 bots" in the Bots menu, "Regear" in GM mode (`/sbl gm`)
### Combat
- **Interrupts shared out** in bot groups: one kick per enemy cast, none wasted on a cast already kicked; heals,
  crowd control and long casts first (`AiPlayerbot.CoaInterruptCoordination`, on)
- **New attack loop** (from StevenLeclerc's work, measured at the bench): spells refused for a passing reason are
  retried sooner, cleaves wait for melee reach, summons are not recast while up, procs that swap a spell are
  followed (`AiPlayerbot.CoaAttackLoop`, `AiPlayerbot.CoaSpellReplacement`, on)
- **Better rotations for the weakest damage specs** at the boss bench: Ranger, Tinker, Bloodmage, Chronomancer,
  Stormbringer (resources spent when full, key spells moved up)
- **Out of the way of boss casts** in dungeons and raids: melee steps out of point-blank and cone casts, everyone
  leaves a targeted area, ranged keep out of reach (`AiPlayerbot.CoaBossKnowledge`)
- **Dungeon tank in front**: out of a fight the tank walks a few yards ahead of you, so it leads and pulls the next pack
### World
- **More bots at high level**: bot levels follow the battleground brackets (1-9 ... 50-54, 55-59, 60), 35% of the
  bots at 60 — battlegrounds 50-59 and 60 start, raids fill on both factions. The bots move over gradually
  (about 20 a minute) after the first start
- **Battlegrounds**: bots fill the faction a real player's battleground lacks to start ("21/20" that never started,
  #5826); bots are no longer teleported out of a battleground after dying
- **"lfg bot"**: a bot that whispered you an offer stays free for you until you invite it; offers last 2 minutes
### Fixes
- Bots no longer abandon the quests they do with you; `bot add "Name Bot"` works with two-word names
- CoA classes no longer learn the spells of their fallback class's quests (#5714); Earthmother's Binding is not
  taken for a heal (#5844); a bot keeps one buff of an exclusive pair (#5971)
- Bots no longer whisper "Using ... / Eating ..." unless you asked them to use something
- Crash fixes (whispered item commands, chat), no overflow on targets above 4.29 million health
### Installing
- Same repack as 1.6: no core update, no client update. Stop the server, extract the CoA-Bots zip into the repack
  (replace the files), run `CoA-Bots\Installer-Bots.bat`, start with `CoA-Bots\Start_All_Bots.bat`
- Your settings are kept; only the values still at their 1.6 default are moved to the 1.7 one (bot levels, gear,
  lfg offers). A copy of your previous playerbots.conf goes to `CoA-Bots\Backup-1.7-<date>`
- `CoA-Bots\Tools\Migrate-Accounts.bat` brings accounts and characters from an older repack, and now checks first
  that both servers are stopped

## v1.6 (30 September 2026) — for repack main-20260930-df4dea11
### Performance
- **5000 bots in under 9 GB**: Seth's memory rework (shared AI registries, trigger definitions and packet
  handlers instead of one copy per bot) is in. Measured with 5000 random bots: 8.6-8.9 GB, stable, no crash
- **The lag with many bots came from the core, and it is fixed**: a Destiny Weaver display refresh walked every
  player of a map for every creature, and bots kept it running all the time (42% of the server CPU with 5000
  bots). Jealous merged the fix (jealous-sound/azerothcore-wotlk-coa#5770): median server tick 279 ms -> 52 ms
- **Bots log in much faster**: one bot login that never completed stopped all the others; now it is set aside
  after 60 seconds (from HatredPsycho's fork). 5000 bots were online in 4 minutes instead of about 12
- Even with every bot fully active (`BotActiveAlone = 100`, no smart scale), 5000 bots now run at 60-240 ms
  per server tick instead of 0.5-6 seconds
- Bots find their glyphs without walking the whole item list (562,000 items) at every refresh

### City life — `AiPlayerbot.RpgStatusProbWeight.GoCity` (10)
- **Bots now spend time in their capitals**: now and then a random bot goes to the bank, auction house or inn of
  a capital of its faction (walking when it is near, taking the hearth otherwise), visits the city NPCs for 10
  to 30 minutes and goes back where it came from. Set the weight to 0 to turn it off

### Dungeon Finder
- **Tanks and healers get the seats**: damage dealers mostly hold back while a queued player still lacks a tank
  or a healer (Zyth45/mod-playerbots#11)
- **Random Heroic works after a Random Mythic**: random bots stayed queued for ever once they had queued, so
  the next queue found no free bot. A bot now leaves the queue once no player waits for its dungeon, and queues
  for one random dungeon at a time
- **No more bot stuck at a dungeon door**: the finder offers dungeons by the DBC level range, the entrance checks
  another one. Bots now skip dungeons their level cannot enter, and one sent there anyway leaves the group
  instead of being revived and sent back every 5 seconds (#5725, #5159, #4818)
- **Razorfen Kraul and Shadowfang Keep strategies** (from HatredPsycho's fork)

### Dungeons and raids
- **Bots follow you into your raid, not into an old one**: a random bot that had killed a Molten Core boss
  with someone else kept that week's lock, went into the other Molten Core and "Regroup" never brought it
  over (jealous-sound/azerothcore-wotlk-coa#5557). A random bot joining a player drops its other instance
  locks, and a summon moves it into the player's own instance
- **The tank no longer pulls the Shadowfang Keep prisoner**: in a group mixing factions, the auto pull
  picked Deathstalker Adamant or Sorcerer Ashcrombe, hostile to the bot but not to the player, and the
  courtyard door never opened (#5552, #5395). The auto pull now only picks a creature hostile to the player
  too, and never one that talks or gives quests

### Bots in the world
- **No more walking through floors and stairs** in dungeons: bots take a real path at the right height, never a
  straight line through the geometry (Zyth45/mod-playerbots#12)
- **Altbots with a two-word name** ("First Last") can be added and summoned (#5198)
- No resurrection spam at a corpse behind a door or a wall (#5722)
- No eating or drinking while swimming (#5533)
- No endless loop on a worldforged pickup already taken (#5519)
- Random bots no longer get Death Knight faces (from StevenLeclerc's fork)
- **Quest shares work every time**: after a group quest the core left a bot marked "busy", and every later
  share was refused with "busy" until it logged out again (#5606)
- **Bots under level 40 mount again** behind a player on a fast mount: they own only their slow mount, which was
  filtered out, and they walked (#5596)
- **Bots stay in the level 60 world**: an older `playerbots.conf` (kept when updating) left Hellfire Peninsula at
  levels 58-66 and Zangarmarsh at 60-67, so level 58-60 bots went to Outland on a level 60 realm. When random
  bots stop at 60, the Burning Crusade and Wrath zones are now left out whatever the file says (Zyth45#14,
  jealous-sound#5831)
- **No more Alliance bots in Ghostlands**: flights through neutral flight masters carried bots into the other
  faction's zones (#5324)
- A main-hand-only weapon is no longer rolled "Need" for an off hand it cannot go in (#5592)
- No more sit/stand loop under the Sun Cleric's Rejuvenating Rays (#4572)
- **Bots keep the quests you share**: grouped with you, a bot no longer tidies its quest log, so grey or elite
  dungeon quests you share stay (#5776). On their own, bots only hand in an elite or group quest they carry,
  they no longer try it alone and die on it (#5521)
- **Smoother following**: out of combat a bot no longer stops for a spell it might not cast, which made it stutter
  behind you (#5595)

### Classes
- **Melee area attacks wait until the enemies are in reach**: a Knight of Xoroth Meatsaw or a Guardian Broad
  Sweep was cast on the pull, from 30 yards (#5500, #5453)
- **Summons are not recast while they are up**: the Cultist's Tentacle of Yogg-Saron, the Witch Doctor's
  Healing Ward and other summons without a cooldown were recast on every global cooldown (#5739, #5605)
- **No more resurrection spells on enemies**: the authored rotations listed some resurrections as attacks, so
  the Templar cast Spiritual Ascension and the Venomancer Prayer Beads at the enemy, and ten other classes had
  the same lines (#5732, #5633). The dead are still raised out of combat. A player already offered a
  resurrection is not resurrected twice (#5768)
- **Talents stop swapping**: two Character Advancement entries of one free-choice group, two entries holding the
  same spell, and entries that are part of the class baseline were changed back and forth at every bot refresh
  (hundreds of bots did it; none now)
- **Self heals are used**: a heal that only targets its caster was never cast by any bot; it is now used when
  the bot is in trouble, like the other self-saving spells. Nearby-ally and ally-cone spells count as ally
  spells (from StevenLeclerc's fork)

### Healers
- **Healers heal before it is too late**: the spec rotations put their attacks up to priority 30, and the
  heals that act before a member drops low (heal over time on a member almost at full health, on the tank,
  staying near the tank) sat below them, so they never came. In the dungeon arenas healers ended their
  fights with 64-83% mana while members spent 4.6 s under half health before dying. These heals now come
  right after the urgent ones, above any attack
- **A healer in a group keeps 60% mana back, not 85%**: at 85 it was "saving" nearly all fight long and
  chose its cheapest heals
- **The Chronomancer healer no longer freezes its own group**: Buy Time puts everyone in the area in stasis,
  allies, enemies and itself; it was first in the rotation. A spell refused because the bot is stunned is
  also set aside instead of being retried every tick

### Grouping
- **A bot you group is ready to queue for anything**: random bots queue for battlegrounds by themselves,
  and a grouped one kept its queue, so the Dungeon Finder refused the whole group ("You cannot queue for a
  dungeon while using battlegrounds or arenas"). A bot joining a player's group, by invitation, `lfg bot` or
  the GM recruit command, now leaves its battleground and Dungeon Finder queues (`b88e8e18`)
- **No more trade windows out of nowhere**: bots took any sentence holding an "item word" for a trade
  request, and those words include chat names and qualities ("whisper", "party", "raid", "epic", "head",
  "5g"...): the word "whisper" in Newcomers opened a trade window from the bots. Outside a whisper, only a
  real item link asks a bot to trade now (reported on Discord by mrtheplague, `a9f6d144`)

### Bots use the spells of their level
- **No more spell ranks above the bot's level**: the core grants CoA ranks, Character Advancement entries,
  trainer and class quest spells on the way up only, so a bot brought down in level (random bots are
  rebuilt at a new level regularly) kept those of its old level. 800 of 1000 bots had some: level 10 bots
  cast Brutal Shot (a level 59 ability) for 2000 on a 548 health player. At login, when rebuilt and when
  recruited, a bot now drops every spell a player of its class would not have yet at its level (`a41afe7d`)
- **Character Advancement follows the level**: the level build no longer picks an entry before the level
  the Character Advancement offers it, entries above the build are lowered and the higher ranks of an
  ability taken away go with it
- Measured on the test arenas: abilities used 20+ levels too early -81%, hits for a player's whole health
  -40%; in dungeons (level 60) median damage +21%, success rate unchanged
- **Templar Blade of Faith and Witch Doctor Malefic Arrow follow the level too**: the core lists some ranks
  with an unknown trainer level, and no table knows the Malefic Arrow ranks, so level 20 bots kept Blade of
  Faith rank 9 (level 58) and Malefic Arrow rank 5 (level 36). Such a rank is now due at its own spell level

### Stability
- **No more crash on a group request without a sender**: a random bot without a master that received an
  "lfg" request whose sender was gone crashed the server (mod-playerbots `46e3d5f8`)
- **No more crash near the new Call Boards** (Jealous's cores after 27/09, mod-hero-call-board): a bot
  looking over a quest giver with more quests than a quest menu can hold (the Stormwind and Orgrimmar Call
  Boards carry 47) tripped a server assertion. Bots now leave such boards to players (`efacf4a2`). **Needed on any core
  that has the Call Boards**

### Bloodmage stops freezing itself
- **Moon Gaze is no longer cast as a buff**: it puts the Bloodmage in Cursed Form and stuns it for five
  minutes. 1.5 took it out of the spec rotations, but the bots' own spell classifier still counted it as a
  buff to keep up (one of its effects is a plain 5 minute aura) and recast it whenever it ran out: the bot
  stood still, healers included (#4836, #5143, #5196, #5360). A spell that puts its caster in a form, or
  stuns or pacifies it, is never treated as a buff now

### Battlegrounds
- **Level 60 battleground brackets**: the automatic battlegrounds (`AiPlayerbot.RandomBotAutoJoinBG = 1`) used
  the upstream level 80 brackets, so on a level 60 realm they never started a battle. The defaults now follow
  CoA's own brackets (PvpDifficulty.dbc): Warsong Gulch and Arathi Basin 60-69 (bracket 5), Alterac Valley 60
  (bracket 0), Eye of the Storm off, arena bracket 60-64. Existing playerbots.conf files are kept: see the
  settings in the guide

### Bots stop whispering every quest step
- **Installs that started on CoA Bots 1.2 or 1.3 get the quiet default**: those versions shipped
  `AiPlayerbot.BotsWhisperPublic = 1`, so every bot whispered its master each quest item picked up, quest
  taken and quest turned in. 1.4 made 0 the default, but an update keeps your playerbots.conf, so early
  installs still had 1. On the first start with bots, CoA Bots sets it to 0 once and says so; set it back to
  1 if you want those reports, it will not be changed again (reported on Discord by Gristwood)

### Setup
- **The bot database follows the repack's MySQL**: `playerbots.conf` kept the MySQL port and password of the day
  CoA Bots was installed, so a repack moved to another port stopped the bot server at startup. The bot server
  now takes them from the repack's own settings at every start

### Logs
- **Playerbots.log and LFG.log are capped too**: CoA Bots already keeps the bot chat, loot and bot report
  logs to their last 24 hours and Server.log / Errors.log to 50 MB. A Playerbots.log or LFG.log switched to
  debug to chase a problem could still grow without end (1.4 GB in two days on our test server); they are
  now held to 50 MB as well
- **Building from source?** The core's worldserver.conf.dist sets `Logger.playerbots=5,Console Playerbots`:
  every bot decision, at debug level, printed to the console. With hundreds of bots it costs real server
  time. Use `Logger.playerbots=4,Playerbots` (or remove `Console`); the repack is not affected

### SquidBots Lite 1.5.2
- **No more Lua error** ("bad argument #2 to 'insert'") when a bot lay dead out of range or on a flight
  path (jealous-sound/azerothcore-wotlk-coa#5441)
- 1.5.1: role badges again for bots named "Name Bot"

## v1.5 (27 September 2026) — for repack main-20260925-6557290fd

**Same repack as 1.4.** Already on CoA Bots 1.4? Stop the server and run `CoA-Bots-Patch-v1.5`: it replaces
the bot server, updates the bot rotations and adds the new settings. Your characters and bots stay.
Most of what follows was reported on Discord and on jealous-sound/azerothcore-wotlk-coa, then found and
checked in game.

### Dungeon Finder
- **Every bot gets in.** A bot sends no "landed" signal, so after a dismount, a jump or a slope it could stay
  "falling" for the server, and the finder refused to teleport it: part of the group stayed outside
  (#4299, #4818, #5372). The stale flag is cleared, and a bot of a finder group left outside the running
  dungeon - dead, a ghost, falling, or released at a graveyard after a wipe - is revived and sent in
  within 5 seconds
- **The tank pulls on its own and the healer says "Low on mana"** in finder groups too: both waited for a
  master, which finder bots do not have

### Tanks and threat
- **Tank stances where they belong.** The tank stances (Agonizing Presence, Twilight Domain, Demonic Power,
  Heart of the Mountain, Lunar Authority, Sol Invictus, Divine Stand, Night's Watch, Tower Formation...)
  were in the rotations of 22 damage and healer specs: a damage-dealer bot in its tank stance pulled the
  monsters off the real tank (#5320, #4462). They are gone from those specs, and a damage dealer or healer
  wearing one steps out of it. Tanks now take theirs first (#5063, #5173)
- **Tanks buff themselves again.** Since 1.4, giving a tank its auto pull quietly took its out-of-combat
  buffs away: tanks never buffed between fights nor put their stance on before the pull
- Stances are recognised by their whole family: a Runemaster no longer wears three tattoos (#5059), a
  Venomancer no longer fights in Sea Serpent Form (#5173), and Reaper's two "Dominion" are told apart

### Healers
- **A healer out of reach walks back.** Out of healing range of the tank, or out of its sight, it comes
  back before casting anything else. A Cultist healer left on the Deadmines platform used to wand through
  the whole boss fight (#5094)
- **Starcaller Moon Priest heals by fighting**, as players do: Lunar Eclipse, Lunar Lance and Moonflow
  trigger Aspect of the Goddess and Moonwater on the group. It never takes a wand (its heals go through
  Huntress Shot) and keeps casting whatever its mana (`AiPlayerbot.CoaOffensiveHealerSpecs = 40,43`)

### Bots that stood still or did too much
- **Bloodmages no longer sit five minutes in Moon Gaze** (#4836, #5143, #5196, #5360, #5375)
- Rangers no longer cast Falconstrike on its own: as for a player, it comes from Quick Shot (every fifth one,
  or the next one after a Horn). The flock of falcons around a Ranger is Falcon's Call, as designed
- **No more parachutes**: Tinkers no longer cast Parachute Pack in fights
- **Bots keep hitting a feared monster** instead of letting it run
- **Bots mount when you mount** (level 20+), and get off when you do; a Venomancer leaves its Spider Form to
  ride. The CoA classes were missing the base strategy that checks it

### Groups
- **Recruited bots are of your faction**, even on a realm that allows cross-faction groups: lfg bot,
  `.playerbots coa` and SquidBots used to bring Horde tanks to Alliance players, attacked by guards and
  deaf to party chat (#5266). `AiPlayerbot.CoaRecruitSameFaction = 0` restores the old behaviour
- **Bots take a quest you share** with the group at once, finder bots included (#5074)
- **No buff over its greater form**: a Witch Hunter bot no longer puts Knight's Edict on someone who wears
  Greater Knight's Edict (#5385)
- Mana potions restore mana: Rage Potions are no longer drunk by casters (#5077); an off-hand item is no
  upgrade under a two-handed weapon (#5081)

### From HatredPsycho's fork — thanks!
- **Low-level CoA bots are no longer naked**: under level 5 they get their class starter kit. Bots left
  naked by an older version are dressed at their next login (Zyth45/mod-playerbots#8)
- A random bot population capped at level 1 keeps the levels it earns
- A party sentence starting with "do" ("do you remember…?") no longer gets one "unknown action" per bot;
  a whisper still reports a mistyped order
- With bot whispers off, bots still answer a click on a quest giver (which quests they can take or hand in)

### Tell bots from players — `AiPlayerbot.CoaBotSurname` (on)
- Every random bot carries the surname "Bot" (**Kegarink Bot**) on nameplates, tooltips, chat and /who,
  with no addon. Set it to 0 and the bots take their own names back at the next start
- To whisper a bot by typing its name: `/w "Kegarink Bot" hello`

## v1.4 (26 September 2026) — for repack main-20260925-6557290fd

**Now built on Jealous-Sound's repack of 25 September.** The bot server is the core of that repack, to
the commit, with the bots module built in. It brings no core change of its own and no core database
update, except one: the dungeon ends below. Already on CoA Bots 1.3? Install Jealous-Sound's full repack of
25 September in a new folder, this package in it, then bring your accounts and characters over with
`Migrate-Accounts` and run `CoA-Bots\Purge-Bots.bat`. Your characters stay.

### Dungeon Finder
- **Every dungeon ends again.** Ascension splits Wailing Caverns, Gnomeregan, Uldaman, Blackrock Depths and
  Scholomance into wings, each one its own dungeon in the finder, and none of them had a last boss on the
  server: kill it and nothing happened, no "Dungeon Complete", no reward. Each wing now ends on the boss the
  client names for it (Lord Cobrahn, Skum, Verdan, Mutanus, Electrocutioner 6000, Crowd Pummeler 9-60,
  Thermaplugg, Ironaya, Grimlok, Archaedas, Golem Lord Argelmach, Gandling, Ras Frostwhisper). One database
  update, the same file as jealous-sound/azerothcore-wotlk-coa#5164
- Already in the repack, from our side: the finder offers classic dungeons only, never one without an
  entrance (Tor'Watha and co.), and Random Classic runs up to level 60 (#5132); bots skip the wardrobe
  and mount collections, 2 GB less memory at 1,000 bots (#5107); trials are no longer greyed out (#5157)
- The quest tracker still reads "Defeat: Unknown" in a finder dungeon. Display only: the dungeon ends and
  rewards as it should

### A clean start for the bots
- **`Purge-Bots.bat` deletes the old bots** (the patch does it too), with the bots module's own cleanup, and they are
  created again at the next start. Bots made by 1.3 still carried up to 2,400 mount and companion spells each, reloaded at every
  login. Only the random bot accounts go: players' accounts and characters are never touched

### Bots stay in Azeroth
- Level 58 to 60 bots no longer travel to Hellfire Peninsula and Zangarmarsh: those zones start at 61 now
- The login message says which CoA Bots you run: "CoA Bots v1.4"

### Dashboard: the bots, live
- The server writes a snapshot of every bot every 5 seconds (`AiPlayerbot.CoaStatusFile`): the dashboard
  shows them on the world map, with a card per bot — what it does, its group, its quests. Under a
  millisecond of server time at 1,000 bots

### SquidBots Lite 1.5
- Passive: the bots stop, and the tank no longer pulls on its own; any group order wakes them up
- Smaller captions, buttons spaced by the widest one


### Bots play their specialization now – `AiPlayerbot.CoaSpecRotations` is on
- Each CoA specialization has an authored rotation. It was held back in v1.3 because the ordering had only been confirmed up to level 40; the whole of this release was measured with it on, several hundred arena fights at level 60
- The runtime classifier stays underneath, so interrupts, defensive cooldowns and the fallback attack are never lost. Set it back to 0 for the old behaviour
- It applies to every CoA bot on the realm, wherever it is – alone in the world, in your group, in a dungeon

### Rotations checked against real players
- A player gave us 1.6 GB of mythic+ combat logs from the official realm. Every specialization can be recognised in them by the spells it casts, which finally gives a reference for what a competent human plays
- The same defect came back on thirteen specializations: the spell that carries the specialization sat low in our priority list, so the bot never reached it. Witch Hunter Houndmaster held the record – the spell its players cast 215 times was at priority 1, the lowest there is, and produced nothing at all
- Fourteen lines corrected. Six spells that never came out now carry 10% to 19% of their specialization's damage: Hexfire, Obliteration Beam, Hunting Shot, Sargeron Smite, Umbral Blade, Air Strike
- Measured honestly: only Tinker Mechanics clears the noise so far, at +40%. Specializations we did not touch moved between −23% and +11% over the same window, which is the size of the noise. More measuring to come

### Healers
- No silent healer left. Starcaller went from 290 healing a run to 7109, Cultist from 18 to 49 healing a second
- Healers no longer burn their mana on damage above 35%, and keep it for heals in a group
- Nine rotation families repaired: heals cast out of range, buffs on the wrong target, a ground heal placed away from the tank, a filler that ate 43% to 79% of a healer's mana
- `AiPlayerbot.CoaHealsExcluded` sets aside a heal that heals nothing, by name. Ships with Nanobot Reconstruction, whose own description reads "healing them for 0 over 1 min"
- Tinker Invention and Venomancer Vizier, set aside in v1.3, are back: both heal properly again, 102 and 122 a second

### Tanks
- Eleven of the twelve tanks hold their role. Templar Oathkeeper loses the fewest group members, Knight of Xoroth deals the most damage
- Bloodmage Eternal is set aside (`AiPlayerbot.CoaExcludedSpecializations = 99`): 0 taunts, 90 damage a second against 289–668 for the others, 4.2 group deaths a run. The cursed form `Eternal Curse` is granted by nothing in the core, so every ability of the specialization fails. Nothing on the bot side can repair it; reported upstream. The Bloodmage stays playable in its three other specializations

### Finding bots without GM commands
- Type `lfg bot tank` or `lfg bot heal dps` in the Zone or Newcomers channel and a free bot of that role answers with an offer you can accept

### SquidBots addon – replaces UnBot
- Group cards, five orders, a recruit panel, offer cards read from the bots' whispers, and badges from party chat: low on mana, pulling, resurrecting
- No window: a thin bar of orders, roles drawn on the party frames you already have, and offers as toasts

### Install & update
- The installer and the patch now ask for your repack folder when they cannot find it themselves, in a normal Windows folder dialog, instead of refusing and asking you to extract them again
- The addon is installed for you: the setup asks for your game client, checks it holds Wow.exe, and copies the addon in. What the addon saved in WTF is not touched
- The dashboard is offered rather than imposed, and removed if you decline
- The patch sets `AiPlayerbot.BotsWhisperPublic` back to 0 if it is still at 1. v1.3 shipped it at 1 by mistake, against its own documented default, and a player writing a single word in a channel was answered privately by a crowd of bots. Along with the two Outland brackets, it is the only value of yours this patch changes; it says so while it does it, and you can set it back
- `-Client <path>`, `-NoAddon` and `-NoDashboard` skip the questions for anyone scripting an install

### Settings
- `AiPlayerbot.CoaSpecRotations` = 1 (new default)
- `AiPlayerbot.CoaGroupTelemetry` = 0 – it wrote a block to the log after every group fight, which costs something on a realm carrying hundreds of bots. Turn it to 1 to measure a run
- `AiPlayerbot.CoaExcludedSpecializations` = 99
- `AiPlayerbot.CoaHealsExcluded` = "Nanobot Reconstruction"

## v1.3 (20 September 2026) — for repack main-20260919-b3717c137

Applies on top of CoA Bots v1.2 or v1.2.1 with the patch, or installs on its own with the full package.

**The bot server also brings the core forward**
- Built on core `ae33455b` (PR #4366), sixty-nine commits past the repack's `b3717c1` (PR #4131): every fix
  Jealous-Sound published since 19 September comes with it — the personal-bank crash (#4303), creature level
  scaling no longer frozen by pets and traps (#4278), the Ascension client's "Reset all Dungeons" opcode
  (#4125), the Chronomancer audit and its 32 fixes (#4324), the Ethereal Bazaar and its vendor Tiraxis (#4345),
  the realm type the client needs or the Vanity Collection stays empty (#4340), starting action bars, valley
  graveyards, and some forty class corrections.
- The database updates those fixes need are applied by the installer or the patch: fifty for the world, and
  ten more that came with the modules they added. Two of those create a new table in the characters database
  (the Craftsman's Codex and the Ethereal Bazaar keep their own state there). Nothing existing is modified:
  no row, no column, no character, no account.
- Our own fix merged upstream: the dynamic XP module no longer announces `.xp` to the whole realm on every login
  (PR #4279).

**The crash that has been taking realms down since Brewfest**
- A creature destroyed while something still followed it left that follower holding a pointer to freed
  memory; the follower wrote through it from its own destructor and the map thread died on the spot
  (`AbstractFollower::SetTarget`, under `MotionMaster::DirectDelete`). Leaving the world detached followers,
  being destroyed did not. Seen six times on three different machines and three core revisions, always the
  same stack, and always more often the more bots are online: about three hours at 200, half an hour at 1000.
  An instrumented build named the culprit - the Thunderbrew Festive Keg summoned by the Brewfest event that
  started on 20 September, with up to five followers detaching at once.
- Followers are now detached when a unit is destroyed, not only when it leaves the world.

**Bots**
- **Custom classes now wear the armor they are proficient with** — until now every CoA class fell through to
  cloth, and no CoA class could hold a shield: a Guardian would leave an item level 200 plate chest in its bags
  and keep item level 11 cloth on. Contributed by **StevenLeclerc**.
- **Professions, forms, stances and summons are out of the damage rotation.** The most cast "attack" on a live
  realm was Bushcraft, a profession skill; forms, permanent auras and proc buffs followed. Measured again after
  the change on 200 bots: none of them is left in the list. Contributed by **StevenLeclerc**.
- `AiPlayerbot.CoaClassesOnly` (on by default): random bots are only created in the 21 CoA classes, whatever
  `CharacterCreating.Disabled.ClassMask` says. Built from source, a realm used to fill its starting valleys with
  Warriors and Mages no player can roll.
- `AiPlayerbot.BotsWhisperPublic` (on by default): at 0, bots stop whispering players unasked. Answers to a
  player's commands, and a bot's hello and goodbye, are always sent.
- `Who.ShowBots` in `worldserver.conf` (on by default): at 0, bot sessions are left out of the /who list and its
  counts. Everything else is unchanged — bots are still seen in the world, in chat, in groups and in the friends
  list. Written here and **merged upstream as PR #4329**, so it is part of the core from now on.

**Dungeon Finder**
- The five dungeons with no entrance are out of the Dungeon Finder until their coordinates exist:
  Vaults of Inquisition, Road to De Other Side, Tor'Watha, Forgotten Mine and Blackrock Caverns. The core
  has no entrance for them in `lfg_dungeon_template` or `areatrigger_teleport`, so the queue popped, the
  teleport failed with "invalid teleport location" and the group broke up. They are locked for everyone in
  the finder, in the explicit list and in the random draw alike; a player who can reach the door still walks
  in. One `DELETE` undoes it, written at the top of the file that applies it.

**Housekeeping**
- The console logs are rotated when the server starts, above 512 MB, keeping one previous copy. A test realm
  left running had let `world-console.log` reach **29.7 GB** while every other log stayed between 1 and 25 MB.
  They cannot be cut while the server runs: it inherits their handle and keeps its own position in the file,
  so a cut one would be padded back to that position with zeros.
- Trimming a log by size now reads only its tail, instead of the whole file.
- The Craftsman's Codex and the Ethereal Bazaar modules ship with their configuration files. Without them the
  server looked their settings up again on every spell a character forgets and every creature killed - constant
  with bots - and logged a warning each time: over 100 lines a second, 100 MB of `Server.log` in a quarter of an
  hour. Every value in those files is the module's own default: nothing plays differently.

**Credits**
- **StevenLeclerc** for the armor proficiencies and the spell classification.
- jealous-sound for the Conquest of Azeroth server and the repack.

## v1.2 (19 September 2026) — for repack main-20260919-b3717c137

**Server**
- The bot server is built on the same core revision as the repack (`b3717c1`): Jealous-Sound's damage and creature
  health corrections (#4131) and every fix since v1.1 are in it.
- **Much lighter with bots**: the challenges module no longer queries the database on every hit (PR #4078, merged),
  and the hunger update no longer reads its settings for players without a hunger challenge. A test realm stays at
  20-60 ms per world update with 1000 bots, where it went past 100 ms, and up to 2.6 s.
- A crash fixed in the bots: two items made from each other (enchanting essences) sent a bot into endless recursion.

**Specializations, gear and talents**
- Authored rotations for all 70 CoA specializations, 2,544 rules generated from the ascensionsidekick.com guides
  (by steviecraycray), behind `AiPlayerbot.CoaSpecRotations` (on in this package).
- Every specialization has its role and its position (melee or ranged).
- Gear is chosen with each specialization's stat weights and the weapon its spells require; the off-hand follows,
  and thrown weapons are offered for the ranged slot.
- `talents spec <name>` (or `tank`, `heal`, `dps`, `random`) switches a bot's specialization and spends its talents.
- The role mix is fixed: bots were 76% damage, 14% tanks, 9% healers; each now plays the role it was given, and keeps
  it across restarts.
- Bots keep the spells they learn after logging out, receive Ascension's runtime spell swaps, and accept the
  starting-zone quests that are accepted automatically.
- A crash fixed when a talent names a talent tab the DBC does not define.

**Healing and combat**
- Healers keep 35% of their mana for heals, other bots that know a heal 15%; below it they fight with what costs
  nothing.
- A spell is only offered when the bot can pay for it, and the bot stops asking every tick for one it cannot.
- One buff per spell category, so bots no longer cycle buffs forever.
- Bots no longer open scripted loot containers.

**Grouping**
- `.playerbots coa tank|heal|dps` brings a bot rebuilt at your level, down as well as up.
- An invited bot answers at once (it used to take 15 to 20 seconds).
- Bots name their class and specialization in chat instead of "(0/0/0)".

**A living world**
- Bots spread over nine level brackets, rebalanced every 5 minutes; about two thirds are below level 30, and
  brackets with real players draw more bots.
- New level 1 bots keep appearing in the eight starting valleys, shared evenly between each faction's valleys.
- Bots talk in the zone channel the client actually shows, rejoin it when they change zone, and use the realm-wide
  channel only when allowed to.

**Dashboard**
- The new SquidBots dashboard: bots online, level spread and roles of the bots online, class podium, experience per
  hour, what the bots say, their rare and epic finds. English and French.

**Checked**: the changes ran overnight on a 1000-bot test realm (about 2,600 bot attacks a minute and 40,000 kills an
hour); the one crash of that night is the recursion fixed above. This package was installed and started on the
19/09 repack: 200 bots online 90 seconds after the world opened.

## v1.1 (16 September 2026) — for repack issue-batch-20260915

Weapon and Necromancer crash fixes, ranged bots melee in reach, bots cast their highest spell rank.
