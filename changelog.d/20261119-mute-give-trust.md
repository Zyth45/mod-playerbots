### Mute, give to alt, trust
Three new features for the SquidBots addon, on by default:
- **Mute** (`AiPlayerbot.CoaMute.Enable`): silence the chatter of all your bots at once, alts and recruited, kept between sessions. A muted bot still answers your orders and questions.
- **Give to alt** (`AiPlayerbot.CoaGiveToAlt.Enable`): one of your alts gives items to another, bag to bag, when both are in your group. Items bound to a character are refused; heirlooms and other account-bound items are fine.
- **Trust** (`AiPlayerbot.CoaTrust.Enable`): each bot shows how much you trust it in the Info tab, a rank and a bar that grow with the monsters (+1), bosses (+25) and raid bosses (+100) you kill together. Shown only: no rank opens or locks anything. New table `playerbots_coa_trust` in the playerbots database. The points and the rank starts are settings (`AiPlayerbot.CoaTrust.Kill`, `Boss`, `RaidBoss`, `Ranks`).
