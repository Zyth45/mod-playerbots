---
area: bots
type: added
audience: players
title: Bots no longer run battlegrounds of bots only, and earn honor in simulated ones
---
Random bots no longer start battlegrounds and arenas with no real player in them (AiPlayerbot.CoaWorld.BgSim.ReplaceAutoJoin, on by default): real battles start when a player queues, and the bots fill the queue at once. With AiPlayerbot.CoaWorld.BgSim.Enable (off by default), bots play simulated Warsong Gulch and Arathi Basin matches offline and come back with the honor a real match gives; they may spend it on PvP gear sold for honor by a vendor in the world.
