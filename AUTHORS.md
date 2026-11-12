# List of AUTHORS who contributed over time to the mod-playerbots project

## Point of current development
The project is currently developed under https://github.com/mod-playerbots/mod-playerbots, as a module for AzerothCore (https://www.azerothcore.org/)

## History of development
mod-playerbots continues the playerbot AI lineage, which was developed under various umbrellas over time:
* Playerbot AI core by ike3, originally developed for the MaNGOS/TrinityCore family
* CMaNGOS playerbots, located at https://github.com/cmangos/playerbots
* mod-playerbots, 2021-CURRENT, located at https://github.com/mod-playerbots/mod-playerbots

## Authorship of the code
Authorship is assigned for each commit within the git history, which is stored in this git repository:
* github.com/mod-playerbots/mod-playerbots

## Authors List:

*Please inform us, if you find somebody who is missing!*

* achyles222
* Aldori
* Alex Dcnh
* Alvin Zhu
* antony
* Atidot3
* avirar
* AzoraNova
* bash
* BeardBear
* Ben Wagner
* Benjamin Jackson
* blinkysc
* Bobblybook
* Bogir[rus]
* Boidl
* Boxhead78
* Brian
* Caffarius
* chris
* Chris Lacy
* Christoph Wilk
* Chromie-WoW
* Crow
* Cryo81
* CrypticAvacado
* Dennis
* dillyns
* Dreathean
* Dustin Hendrickson
* Engardium
* EricksOliveira
* Fiery
* Flashtate98
* FutterSillo
* Fuzz
* Gabriel Comeau
* Garashan
* Gonzalo
* gtkk
* HennyWilly
* Hokken
* hongyiqin
* HT Cesta
* Iain Donnelly
* Ivan Novokhatski
* Jay
* Jelly
* Jered
* Jgoodwin64
* jimm0thy
* jublinq
* Julien MAS
* kadeshar
* Keleborn
* kikiviki
* killerzwelch
* Lichborne
* Mikhail
* mostlynick3
* mtm84
* nick
* Nicolas Lebacq
* Noscopezz
* Not
* NotCronky
* NoxMax
* Oiuv
* OlegKungurov
* oskov
* Paulo
* Paulo Oliveira
* privatecore
* qcloud
* qudzy
* Revision
* Rikus Louw
* SaW
* Scarecr0w12
* sd3420625
* secahtah
* Spargel
* St0ny
* sudlud
* Tecc
* ThePenguinMan96
* Thomas Frey
* tomcoffingiii
* Trus3683
* Type1Error
* UltraNix
* valsan-azerty-boi
* Vanna White
* Veit F.
* Vigerus
* wetbrownsauce
* whipowill
* xSparky911x
* XYUU
* yang
* Yunfan Li
* zeb139
* ZhengPeiRu21
* 姜耀

## Code under another license

* The combat guards in `src/Ai/Coa/Gardes/` and the reference headers in `tests/dc294/` come from Dungeon Clear by Jared Wright and contributors, under AGPL-3.0-or-later (see `LICENSE-GARDES` and the full text in `LICENSE-AGPL3`).
* The invasion schedule in `src/Ai/Coa/World/` (CoaWorldInvasion*) is adapted from wow-mod-era-events by buildthehomelab, under the MIT License; its notice is kept in `CoaWorldInvasion.h`.

## Code taken from other projects

* The auction house bot in `src/Ai/Coa/Economy/AuctionHouse/CoaAuctionHouseBot.cpp/.h` and its settings file `conf/mod_ahbot.conf.dist` come from mod-ah-bot-plus by Nathan Handley (after the AzerothCore mod-ah-bot, TrinityCore and MaNGOS), under GPL-2.0-or-later; their copyright notices are kept in the source files.
* Ideas of the CoA economy (`src/Ai/Coa/Economy/`), rewritten, no code copied: the no-arbitrage rule, real-sales-only learning and rarity premium after stuhby; the purchase chance and rival bidder after felworld; the learned price band and bots trading at the auction house after aspollon; the deprecated-item filter after danieldfischer and Ithen.
* Ideas of CoA World (`src/Ai/Coa/World/`), rewritten, no code copied: stuck-bot recovery and quest quarantine after Fuitad (GPL-2.0); resource rushes after Old-Man-Warcraft (GPL-2.0); balanced creation of bot guilds after Coralis (GPL-2.0); random bots creating Dungeon Finder demand after Wilhelm8585 (GPL-2.0).
