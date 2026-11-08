---
area: bots
type: added
audience: admins
title: Bot surname in three modes: "Bot", none, or a family name
---
`AiPlayerbot.CoaBotSurname` now takes 0 (no surname), 1 ("Bot", the default, unchanged) or 2: each random bot carries a family name of its own, always the same, drawn from `AiPlayerbot.CoaBotSurnameList` (empty: 64 built-in neutral names such as Stormhand or Ashvale). Applied at the next start; a bot whose new name is taken keeps its own. Your existing 0 or 1 reads as before.
