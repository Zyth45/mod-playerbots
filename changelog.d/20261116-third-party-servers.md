### Servers with added races, and third-party servers
- A server that adds races describes each one in playerbots.conf, without touching the module: `AiPlayerbot.BotRace.85 = "bots=1 gender=female side=horde look=simple names=orc"`. Fields: bots, gender, side, look, names, classes. Without a line, a race behaves exactly as before; a line for an unknown race or with a typo is ignored with an error at startup.
- The side of an added race now comes from its data everywhere: bots of an added Alliance race were filed as Horde in a few places.
- `AiPlayerbot.StartupCheckOnly = 1` (or `CoA.ValidationMode = 1`): the server starts and loads the bots' data, but creates and logs in no bot, to check a build before opening it. Default 0.
- Each release now ships `squidbots-release.json` (version, every SQL file with its sha256, new keys) and an up-to-date `playerbots.conf.settings.json` with the 3.x options.
- A server built without Dungeon Clear says so once in its log at startup, and the SquidBots addon can ask it (`SQBW CAPS?`, `AiPlayerbot.CoaCaps.Addon = 1`): its Dungeon features then show "Dungeon Clear not installed on this server". Servers with Dungeon Clear see no change.
