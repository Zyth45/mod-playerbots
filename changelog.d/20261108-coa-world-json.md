---
area: bots
type: added
audience: admins
title: world.json for the CoA Dashboard (Population and Health), off by default
---
With `AiPlayerbot.CoaLive.DataFile.Enable = 1`, the module writes `world.json` in `AiPlayerbot.CoaDataDir` (default `<LogsDir>/squidbot`). It holds the bots online by zone, what they are doing, their surnames and the server tick. It is written every 10 s, and only while the dashboard asks for it, so it costs nothing when nobody looks.
