### Bots no longer give up on the auctioneer at the first hiccup
In a capital, a bot on its way to an NPC gave it up as soon as one step was refused, even when only that step was refused (the same spot twice in a row, a moment it could not move). It now gives up only when the NPC is gone or no walkable spot leads to it, or after several refused steps in a row.
