---
area: bots
type: added
audience: admins
title: Performance options for many bots (off by default)
---
Two new options to lighten the server with many bots, off by default: the bots' DPS target is kept for one AI pass instead of being searched again on every call (AiPlayerbot.CoaWorld.DpsTargetCache), and bots in a battleground or arena with no real player think every 500 ms instead of every tick (AiPlayerbot.CoaWorld.BotOnlyBgPace.Enable).
