---
area: bots
type: added
audience: admins
title: Bot population follows the hour of the day (CoA Live sessions, off by default)
---
With `AiPlayerbot.CoaLive.Sessions.Enable = 1`, random bots come and go like players. How many are online follows a curve over the day (`HourCurve`, quiet nights and busy evenings, plus a weekend bonus), and each bot stays for a session of a believable length before logging out and coming back later. Fewer bots at quiet hours also means less CPU. Bots in a group, a dungeon or a battleground never leave mid-way.
