### Common crafting materials always at the auction house
- The house seller keeps about ninety common materials of levels 1 to 60 on offer in the Alliance and Horde houses: cloth, ore, bars, stone, herbs, leather, hides, elemental and enchanting materials. Before, a given bar or herb was often missing for days, because the house drew its listings at random among thousands of items.
- Two auctions of each (`AiPlayerbot.CoaEconomy.AHBot.MaterialFloor.Stacks`), full stacks at the market price. Nothing is added when players and bots already offer that many, and the house never sells under the lowest price a player asks.
- On by default (`AiPlayerbot.CoaEconomy.AHBot.MaterialFloor.Enable = 1`); it uses the house seller's usual quota.
