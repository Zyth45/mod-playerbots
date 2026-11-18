### Bots that keep out of sight (options, off by default)
Contributed by stefanw1337 (pull request #66). Three new options, all 0 by default: nothing changes unless a server turns them on in playerbots.conf. **Not verified in game yet**: the checks build and run, but no test has caught a bot in the moment yet.
- `AiPlayerbot.FlyOnlyOutOfSight`: a bot not led by a real player (alone, or following another bot) takes off only while no real player can see it. Seen in the air, it glides down and lands. Bots led by a real player follow their master as before.
- `AiPlayerbot.TeleportOnlyOutOfSight`: a stuck bot teleports only while no real player can see it. While one can, it waits where it is and teleports as soon as nobody watches; still watched 2 minutes later, it gives its target up.
- `AiPlayerbot.StandOffObjects`: bots stop at talking distance from the NPC or object they walk to, instead of walking into it.
