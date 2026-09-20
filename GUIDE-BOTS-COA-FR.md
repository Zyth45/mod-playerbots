# Guide des bots CoA : commandes et réglages

Pour un serveur Conquest of Azeroth avec le module [mod-playerbots branche `coa`](https://github.com/Zyth45/mod-playerbots/tree/coa).
Toutes les commandes ci-dessous se tapent **dans le chat du jeu**.

## En deux minutes

1. **Recruter un bot** selon le rôle voulu, il rejoint ton groupe et te suit :
   ```
   .playerbots coa tank
   .playerbots coa heal
   .playerbots coa dps
   ```
2. **Lui parler** : clique sur son nom dans le groupe, ou `/whisper NomDuBot <commande>`.
   ```
   /w NomDuBot co ?
   ```
3. **Le renvoyer** :
   ```
   .playerbots bot remove NomDuBot
   ```

## Parler à un bot

Le bot doit être **dans ton groupe**, sinon il ne répond qu'à une ou deux commandes.
Un `?` à la fin **affiche** au lieu de changer : `co ?` liste, `co +truc` ajoute, `co -truc` retire.

| Commande | Ce qu'elle fait |
|---|---|
| `co ?` | ses stratégies de combat : sa position (`close` / `ranged`), notre `coa`, et sa rotation `custom::classe-spé` |
| `nc ?` | ses stratégies hors combat |
| `talents` | sa spécialisation CoA |
| `talents spec list` | toutes les spés de sa classe, avec le rôle, et un `>` devant la sienne |
| `talents spec <nom>` | change sa spécialisation (`tank`, `heal`, `dps` et `random` marchent aussi) |
| `spells` | ses sorts |
| `stats` | vie, mana, or, sacs |
| `equip` / `autogear` / `upgrade` | son équipement |
| `follow`, `stay`, `flee`, `attack` | son déplacement |
| `formation`, `position`, `rti` | sa place dans le groupe et sa cible marquée |
| `home`, `teleport`, `taxi`, `repair`, `bank`, `trainer` | ses déplacements et services |
| `quests`, `do quest`, `share` | ses quêtes |
| `help` | la liste complète |

## Commandes de maître de jeu

`.playerbots bot` gère tes bots, `.playerbots rndbot` gère les bots aléatoires du serveur.

| Commande | Ce qu'elle fait |
|---|---|
| `.playerbots coa tank\|heal\|dps` | recrute un bot CoA qui joue ce rôle, de préférence proche de ton niveau, le reconstruit exactement à ton niveau (équipement et talents compris, vers le haut comme vers le bas) et le téléporte |
| `.playerbots bot add <nom>` | prend le contrôle d'un bot précis |
| `.playerbots bot addclass <classe>` | crée un bot d'une classe donnée |
| `.playerbots bot remove <nom>` | le renvoie |
| `.playerbots bot list` | tes bots |
| `.playerbots bot self` | pilote **ton propre personnage** comme un bot |
| `.playerbots rndbot stats` | l'état des bots aléatoires |
| `.playerbots rndbot teleport` | **les envoie tous dans une zone adaptée à leur niveau**, sans attendre le téléport automatique |
| `.playerbots rndbot grind` | les envoie chasser |
| `.playerbots rndbot init` | refait leur niveau, leur équipement et leurs talents |
| `.playerbots rndbot levelup` | leur donne un niveau |
| `.playerbots rndbot revive` | ressuscite les morts |
| `.playerbots rndbot refresh` | les soigne et les remet à neuf |
| `.playerbots rndbot reload` | relit `playerbots.conf` sans redémarrer |

Les commandes `.playerbots rndbot` s'appliquent à tous les bots connectés. Pour n'en viser qu'un :
`.playerbots rndbot teleport Nomdubot`.

## Comment le monde se remplit

- Les bots sont répartis en **neuf tranches de niveau** (1-3, 4-7, 8-12, 13-17, 18-23, 24-30, 31-40, 41-50, 51-60),
  rééquilibrées toutes les 5 minutes : environ deux tiers sont sous le niveau 30.
- **Les tranches où jouent de vrais joueurs attirent plus de bots** : avec un perso niveau 15, tu croises plus de bots
  autour du 15.
- **De nouveaux bots niveau 1 apparaissent en continu dans les vallées de départ** (Northshire, Coldridge, Shadowglen,
  Ammen Vale, Valley of Trials, Deathknell, Red Cloud Mesa, Sunstrider), à parts égales entre les vallées de chaque
  faction. Quand ils montent de niveau et partent, d'autres les remplacent.

## Réglages utiles (`playerbots.conf`)

| Réglage | Effet |
|---|---|
| `AiPlayerbot.MinRandomBots` / `MaxRandomBots` | combien de bots jouent en même temps (environ 10 Go de RAM pour 1000) |
| `AiPlayerbot.LevelBrackets.*` | les tranches ci-dessus ; `Dynamic.RealPlayerWeight` règle l'attraction des joueurs |
| `AiPlayerbot.LevelBrackets.FreshStart` / `FreshStartSpread` | les débutants niveau 1 dans les vallées de départ / répartis entre les vallées |
| `AiPlayerbot.RandomBotMinLevel` / `RandomBotMaxLevel` | la plage de niveaux tirée au sort à la première connexion d'un bot |
| `AiPlayerbot.BotActiveAlone` | part des bots actifs quand aucun joueur n'est près d'eux (60 conseillé, 10 par défaut : presque rien ne bouge) |
| `AiPlayerbot.MinRandomBotTeleportInterval` / `Max...` | délai entre deux déplacements automatiques, en secondes (3600 à 18000 par défaut) |
| `AiPlayerbot.GroupInvitationPermission` | à 2, tous les bots acceptent les invitations |
| `AiPlayerbot.BotTextLocale` | langue du chat des bots : 2 pour le français, 0 pour l'anglais |
| `AiPlayerbot.CoaHealerManaReserve` / `CoaCasterManaReserve` | part de mana gardée pour les soins au lieu d'être dépensée en dégâts, en pour cent : 35 pour les soigneurs, 15 pour les autres bots qui connaissent un soin. En dessous, le bot attaque avec ce qui ne coûte rien et son arme. Un bot sans aucun soin n'est jamais bridé, 0 désactive |
| `AiPlayerbot.ZoneChannelId` | numéro du canal de zone, tel que le client le donne. **3 sur CoA** (le canal « Zone - … »), 1 sur un client standard. Mauvais numéro = les bots parlent dans le vide |
| `AiPlayerbot.BroadcastWorldChannelName` | nom exact du canal global. `"Ascension"` sur CoA, `"World"` ailleurs |
| `AiPlayerbot.BroadcastToWorldGlobalChance` | part des messages envoyés dans le canal global, sur 30 000. **0 = les bots n'y entrent même pas** |
| `AiPlayerbot.BroadcastToGeneralGlobalChance` | part des messages envoyés dans le canal de zone, sur 30 000 |
| `Who.ShowBots` (dans worldserver.conf) | à 1, la valeur par défaut, les bots apparaissent dans la liste /who comme les joueurs ; à 0 la liste est laissée aux vrais joueurs. `.reload config` l'applique sans redémarrage |
| `AiPlayerbot.CoaClassesOnly` | à 1 (par défaut), les bots ne sont créés qu'en classes CoA, quoi que dise `CharacterCreating.Disabled.ClassMask` dans worldserver.conf ; à 0, toutes les classes autorisées par le core |
| `AiPlayerbot.CoaSpecRotations` | à 1, les bots suivent la rotation écrite de leur spécialisation en plus du choix automatique des sorts (0 par défaut) |
| `AiPlayerbot.DeleteRandomBotAccounts` | à 1, supprime tous les bots au prochain démarrage, puis en recrée. **Le serveur s'arrête ensuite de lui-même** : remets 0 et redémarre |
| `Appender.CoaBots=2,4,1,CoaBots.log,a` et `Logger.playerbots.coa=4,CoaBots` (dans `worldserver.conf`) | écrit toutes les 10 min quels sorts CoA les bots lancent et pourquoi certains échouent |

## Pièges à connaître

- **`co` tout seul ne répond pas**, il faut `co ?`. Pareil pour `nc`.
- **Hors groupe**, un bot ignore la plupart des commandes.
- **Un bot inactif ne bouge pas** : hors groupe et loin des joueurs, seule une partie des bots est active (`BotActiveAlone`).
- **Un bot peut changer de niveau d'un coup** : c'est le rééquilibrage des tranches. Il est alors envoyé dans une zone
  de son nouveau niveau.
- **Le nombre total de bots dépasse le nombre de bots connectés** : le module garde une réserve et fait tourner les personnages.

## Quand ça ne va pas

- **Un bot ne lance pas ses sorts** : vérifie sa spé avec `talents`, ses stratégies avec `co ?`, et active le journal `CoaBots.log` ci-dessus.
- **Le royaume est « hors ligne »** alors que le serveur tourne : le drapeau est resté à 2 dans `acore_auth.realmlist`, mets-le à 0.
- **Un plantage** : le rapport est dans `Core\Crashes`, garde le `.txt` et le `.dmp`, ils permettent de retrouver la cause.

## Crédits

- [jealous-sound](https://github.com/jealous-sound/azerothcore-wotlk-coa) pour le serveur Conquest of Azeroth
- [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots) pour les bots
- [ascensionsidekick.com](https://ascensionsidekick.com) pour les rôles des spécialisations et les builds de talents
- [steviecraycray](https://github.com/steviecraycray) pour les rotations écrites par spécialisation, les poids
  d'équipement et plusieurs correctifs
