---
area: bots
type: added
audience: players
title: Bots put their weapon away, and no longer revive among monsters or swim forever (off by default)
---
- `AiPlayerbot.CoaLive.Sheath.Enable = 1`: bots draw their weapon in a fight and put it away out of it, on a mount and in flight, as players do.
- `AiPlayerbot.CoaLive.CleanWorld.Enable = 1`: a random bot no longer comes back to life in the middle of the monsters that killed it. It revives a little away from them, or at the spirit healer. A bot idling in the water swims back to the shore after 30 seconds.
