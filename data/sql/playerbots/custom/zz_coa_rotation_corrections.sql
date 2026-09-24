-- Corrections apportees aux rotations CoA, par-dessus le fichier genere.
--
-- `playerbots_coa_rotations.sql` est produit par un generateur a partir des guides publies, et
-- porte « ne pas editer a la main » : le prochain passage du generateur ecraserait toute retouche.
-- Nos corrections vivent donc ici, dans un fichier qui s'applique APRES lui -- l'installateur trie
-- les fichiers par nom, d'ou le prefixe zz_.
--
-- Deux series :
--   23/09/2026  des lignes qui n'aboutissaient jamais, trouvees en comptant les tentatives dans le
--               journal de combat : sorts lances hors de portee, buffs sur la mauvaise cible, un
--               soin de zone pose loin du tank.
--   24/09/2026  le sort qui porte la specialisation enterre bas dans nos priorites, trouve en
--               comparant nos rotations aux journaux de combat de vrais joueurs (mythic+).
--
-- Chaque instruction designe ses lignes par leur CONTENU et non par leur identifiant : les id
-- different d'une installation a l'autre. Rejouer ce fichier est sans effet une fois applique.
--
-- ==================================================================================================

-- Deux lignes de rotation qui n'aboutissaient jamais, trouvées en comptant les tentatives dans le
-- journal de combat (23/09/2026) puis vérifiées sur l'archive Ascension.
--
-- À rejouer sur `acore_playerbots`. Le repack embarque un dump de cette base : une fois appliqué,
-- le correctif part avec la prochaine version. Écrit par contenu et non par identifiant : les id
-- diffèrent d'une installation à l'autre.

-- 1. Gavel of Grace (Sun Cleric) est une compétence « Melee Range ». Deux spés sur quatre la
--    déclaraient en `cast::` : le bot, à distance, lançait dans le vide — 85 échecs mesurés.
--    Seraphim et Valkyrie l'avaient déjà en `cast melee::`, on s'aligne dessus.
UPDATE `playerbots_custom_strategy`
   SET `action_line` = REPLACE(`action_line`, '>cast::Gavel of Grace', '>cast melee::Gavel of Grace')
 WHERE `action_line` LIKE '%>cast::Gavel of Grace%';

-- 2. Guard (Guardian) : « Rush towards an ally, intercepting the next melee attack made against
--    them », portée 8 à 25 yards. C'est une compétence défensive qui vise un ALLIÉ, et qui a une
--    portée MINIMALE. La rotation la lançait en `cast melee::` sur la cible ennemie : mauvaise
--    cible et mauvaise distance à la fois. 331 tentatives, aucune réussite, dans les spés Vanguard
--    et Inspiration. Les lignes sont retirées de la rotation de dégâts ; un tank n'a de toute façon
--    pas intérêt à charger loin de son paquet.
--
--    Lignes retirées, pour pouvoir revenir en arrière :
--      guardian-vanguard    idx 2   'medium aoe>cast melee::Guard!87'
--      guardian-vanguard    idx 6   'can cast::Guard>cast melee::Guard!83'
--      guardian-inspiration idx 30  'can cast::Guard>cast melee::Guard!10'
DELETE FROM `playerbots_custom_strategy`
 WHERE `name` LIKE 'guardian-%' AND `action_line` LIKE '%::Guard!%';

-- 3. Radiance (Sun Cleric) est une zone posée au sol : « Friendly units in the area will be healed
--    every 3 seconds », rayon 8 yards. La rotation la lançait en `cast buff::`, c'est-à-dire SUR LE
--    SOIGNEUR : le cercle apparaissait seize yards derrière le groupe et ne couvrait personne.
--    3415 de mana pour 342 points de soin (23/09).
--    `cast heal tank::` la pose sur le tank, là où se tiennent aussi les corps-à-corps, et
--    `medium aoe heal` la déclenche quand plusieurs membres sont blessés plutôt qu'en permanence.
UPDATE `playerbots_custom_strategy`
   SET `action_line` = REPLACE(REPLACE(`action_line`, 'buff missing::Radiance>', 'medium aoe heal>'),
                               '>cast buff::Radiance', '>cast heal tank::Radiance')
 WHERE `action_line` LIKE 'buff missing::Radiance>cast buff::Radiance%';

-- 4. Phoenix Dive (Pyromancer, spé Flameweaving) : « Command your Phoenix to swoop to the target
--    ALLY, shielding up to 10 allies in its path ». C'est un bouclier posé sur un allié, et la
--    rotation l'écrivait en `cast::`, c'est-à-dire sur la cible en cours — l'ennemi. La même ligne
--    est encadrée, aux priorités 88 et 92, par deux `cast buff party::Inferno Barrier` correctement
--    écrites : on s'aligne dessus.
UPDATE `playerbots_custom_strategy`
   SET `action_line` = REPLACE(`action_line`, '>cast::Phoenix Dive', '>cast buff party::Phoenix Dive')
 WHERE `action_line` LIKE '%party member low health>cast::Phoenix Dive%';

-- 5. Infused Aegis (Starcaller) : « Instantly counterattack any enemy that strikes you from the
--    front IN MELEE for Arcane damage ». C'est une riposte au corps-à-corps, posée sur une
--    soigneuse qui se tient à seize yards du paquet : elle n'est jamais frappée au corps-à-corps,
--    le sort ne peut rien produire. En priorité 27, il repart toutes les dix secondes dès qu'il n'y
--    a rien de mieux à faire, à 922 points de mana — un quart de la réserve à ce niveau. C'est la
--    fuite qui laissait la Moon Priest à 1 % de mana pendant onze secondes (23/09).
--
--    Ligne retirée :  starcaller-moon-priest  idx 16  'buff missing::Infused Aegis>cast buff::Infused Aegis!27'
--    `starcaller-moon-guard` idx 13 porte la même ligne ; on la laisse en place tant qu'on ne l'a
--    pas mesurée, la spé n'étant pas une spé de soin.
DELETE FROM `playerbots_custom_strategy`
 WHERE `name` = 'starcaller-moon-priest' AND `action_line` LIKE '%::Infused Aegis!%';

-- 6. Hand of Elune (Starcaller) : « DRAINS 20% MAXIMUM MANA. Healing them for 0.15, scaling with
--    maximum mana ». Le sort coûte un cinquième de la réserve par conception, ce n'est pas un
--    défaut. Mais il était au palier « santé moyenne », donc lancé en routine : deux incantations
--    et 40 % de la mana partait pour 271 et 495 points de soin. On le descend au palier « santé
--    basse », là où dépenser un cinquième de sa réserve se défend.
UPDATE `playerbots_custom_strategy`
   SET `action_line` = REPLACE(`action_line`, 'party member medium health>cast heal party::Hand of Elune',
                                              'party member low health>cast heal party::Hand of Elune')
 WHERE `action_line` LIKE 'party member medium health>cast heal party::Hand of Elune%';

-- 7. Nanobot Reconstruction (Tinker) : la description dit « healing them for 0 over 1 min », et la
--    mesure le confirme — 2049 points de mana pour 228 points de soin utiles, le pire rendement de
--    toutes les classes (23/09). Il était en priorité 90, le deuxième soin du Tinker : c'est là que
--    passait sa mana. `Repair Shot`, juste en dessous en 89, rend 517 points en une incantation.
--
--    Ligne retirée :  tinker-invention  idx 3  'party member medium health>cast heal party::Nanobot Reconstruction!90'
DELETE FROM `playerbots_custom_strategy`
 WHERE `name` = 'tinker-invention' AND `action_line` LIKE '%::Nanobot Reconstruction!%';

-- 8. Les soins à coût proportionnel, en ligne de remplissage et sur la cible ennemie.
--
--    Trouvés en croisant les rotations avec le journal : un sort qui apparaît dans les blocs de
--    soin et JAMAIS dans les blocs de dégâts, lancé en `cast::` ou `cast melee::`. Sur 21 lignes
--    ainsi repérées, seules celles-ci sont fautives — `Fortify Timeline` soigne tout le groupe et
--    `Healing Ward` pose un totem : la cible leur est indifférente, on n'y touche pas.
--
--      Hand of Elune      « Drains 20% Maximum Mana. Envelop an ALLY in Elune's grace »
--      Touch of Moonlight « Drains 10% Maximum Mana. Quickly heal an ALLY with Elune's touch »
--
--    Les deux exigent une cible alliée, et leur coût est un pourcentage de la réserve. En ligne de
--    remplissage (`can cast::`, priorités 14 à 28), ils repartent dès qu'il n'y a rien de mieux à
--    faire : la Moon Priest y a laissé toute sa mana, 1 % pendant onze secondes (23/09).
--
--    Lignes retirées :
--      starcaller-moon-guard   idx 23  'can cast::Hand of Elune>cast melee::Hand of Elune!16'
--      starcaller-moon-priest  idx 26  'can cast::Hand of Elune>cast::Hand of Elune!15'
--      starcaller-sentinel     idx 23  'can cast::Hand of Elune>cast::Hand of Elune!14'
--      starcaller-warden       idx 22  'can cast::Hand of Elune>cast melee::Hand of Elune!18'
--      starcaller-moon-priest  idx 15  'can cast::Touch of Moonlight>cast::Touch of Moonlight!28'
DELETE FROM `playerbots_custom_strategy`
 WHERE `action_line` LIKE 'can cast::Hand of Elune>%'
    OR `action_line` LIKE 'can cast::Touch of Moonlight>%';

-- 9. Bloodmage Fleshweaver : une spé de SOIGNEUR dont la rotation ne soigne personne.
--
--    Trouvé en croisant notre table des rôles avec la tier list de kami-labs.fr : sur 69 spés, une
--    seule ne concordait pas. Vérifié ensuite sur les fiches des sorts, qui tranchent :
--      Fleshcraft   « Mend blood and bone, healing AN ALLY for 50% of their maximum health »
--      Bloodthorns  « Bloodthorns sprout from AN ALLY ... causing damage to attackers »
--      Blood Veil   « Create a magical veil around AN ALLY, absorbing 50% of incoming damage »
--
--    Les trois visent un allié, et les trois étaient lancés sur la cible en cours — l'ennemi — par
--    un déclencheur de dégâts de zone (`medium aoe`). La même famille d'erreur que Radiance, mais
--    sur une spécialisation entière.
--
--    Fleshcraft rend la moitié de la vie maximale d'un allié et pose `Sated` : c'est un sort
--    d'urgence, pas un soin d'entretien. Il va donc aux paliers « critique » et « basse », jamais
--    en routine. Sanguine Mend, plus modeste, tient le palier « moyenne ».
--    Bloodthorns et Blood Veil demandent le niveau 56 : les lignes restent, corrigées, pour quand
--    les bots y arriveront.
--
--    Lignes d'origine :
--      idx 1  'medium aoe>cast::Fleshcraft!88'
--      idx 2  'medium aoe>cast::Bloodthorns!87'
--      idx 3  'medium aoe>cast::Sanguine Mend!86'
--      idx 5  'can cast::Fleshcraft>cast::Fleshcraft!84'
--      idx 7  'can cast::Sanguine Mend>cast::Sanguine Mend!82'
--      idx 8  'can cast::Blood Veil>cast::Blood Veil!30'
DELETE FROM `playerbots_custom_strategy`
 WHERE `name` = 'bloodmage-fleshweaver'
   AND `action_line` IN ('medium aoe>cast::Fleshcraft!88',
                         'medium aoe>cast::Bloodthorns!87',
                         'medium aoe>cast::Sanguine Mend!86',
                         'can cast::Fleshcraft>cast::Fleshcraft!84',
                         'can cast::Sanguine Mend>cast::Sanguine Mend!82',
                         'can cast::Blood Veil>cast::Blood Veil!30');

-- Rejouable : on efface d'abord ce que ce bloc possede, exactement comme le fichier genere le
-- fait pour chacune de ses strategies.
DELETE FROM `playerbots_custom_strategy`
 WHERE `owner` = 0 AND `name` = 'bloodmage-fleshweaver' AND `idx` BETWEEN 101 AND 106;

INSERT INTO `playerbots_custom_strategy` (`owner`, `name`, `idx`, `action_line`)
SELECT 0, 'bloodmage-fleshweaver', idx, line FROM (
    SELECT 101 AS idx, 'party member critical health>cast heal party::Fleshcraft!92' AS line
    UNION ALL SELECT 102, 'party member critical health>cast buff party::Blood Veil!91'
    UNION ALL SELECT 103, 'party member low health>cast heal party::Fleshcraft!89'
    UNION ALL SELECT 104, 'party member low health>cast heal party::Sanguine Mend!88'
    UNION ALL SELECT 105, 'party member medium health>cast heal party::Sanguine Mend!86'
    UNION ALL SELECT 106, 'buff missing::Bloodthorns>cast buff party::Bloodthorns!24'
) AS rota;

-- ==================================================================================================

-- Trois rotations remises dans l'ordre, d'après ce que lancent de vrais joueurs (24/09/2026).
--
-- Source : les journaux de combat mythic+ du serveur officiel CoA (C:\COA-JS\mythicslogs), fournis
-- par un joueur. On y retrouve chaque spé par ses sorts, et on compare ce qu'un humain compétent
-- lance à ce que notre rotation prescrit.
--
-- Le même défaut revient sur les trois : le sort qui PORTE la spé chez l'humain est enterré bas
-- dans notre liste de priorités, et le bot ne descend jamais jusqu'à lui. Attention en lisant les
-- journaux : les sorts qui affichent le plus de dégâts (Ruin, Clotting, Hexfire Wrath...) ont zéro
-- lancer -- ce sont des effets déclenchés. Seul le nombre de LANCERS dit ce qu'un joueur joue.
--
-- À rejouer sur `acore_playerbots`. Écrit par contenu et non par identifiant.

-- 1. Felsworn Infernal : 121 dégâts/s, contre 927 pour sa jumelle Slayer. Le pire écart du serveur.
--    Le joueur lance `Sargeron Smite` 649 fois -- c'est toute sa rotation, et `Ruin`, `Chaos` et
--    `Felwrath` (60 % de ses dégâts, zéro lancer) en sont les retombées. Nos bots tirent 44 % de
--    leurs dégâts de `Fel Fireball`, que le joueur ne lance jamais, et seulement 9 % de Sargeron
--    Smite. La cause tient en un point de priorité : Fel Fireball était à 79, Sargeron Smite à 78.
--    On échange, dans le bloc de zone comme dans le bloc mono-cible.
UPDATE `playerbots_custom_strategy`
   SET `action_line` = 'medium aoe>cast::Sargeron Smite!87'
 WHERE `name` = 'felsworn-infernal' AND `action_line` = 'medium aoe>cast::Fel Fireball!87';
UPDATE `playerbots_custom_strategy`
   SET `action_line` = 'medium aoe>cast::Fel Fireball!86'
 WHERE `name` = 'felsworn-infernal' AND `action_line` = 'medium aoe>cast::Sargeron Smite!86';
UPDATE `playerbots_custom_strategy`
   SET `action_line` = 'can cast::Sargeron Smite>cast::Sargeron Smite!79'
 WHERE `name` = 'felsworn-infernal' AND `action_line` = 'can cast::Fel Fireball>cast::Fel Fireball!79';
UPDATE `playerbots_custom_strategy`
   SET `action_line` = 'can cast::Fel Fireball>cast::Fel Fireball!78'
 WHERE `name` = 'felsworn-infernal' AND `action_line` = 'can cast::Sargeron Smite>cast::Sargeron Smite!78';

-- 2. Bloodmage Accursed : 159 dégâts/s, dernier du serveur. `Aortic Assault` est le premier lancer
--    du joueur (195 fois) et 28 % de ses dégâts ; chez nous il était en priorité 20 et ne pesait
--    que 4 %. Nos bots passaient 29 % du combat en mêlée et 21 % sur `Scarlet Delirium`, un sort à
--    105 de dégâts par coup que le joueur ne lance jamais. On remonte Aortic Assault au-dessus de
--    Reave (84) et Bloodbolt (83), dont le joueur ne tire que 8 % et 0 %.
--      avant : 'can cast::Aortic Assault>cast melee::Aortic Assault!20'
UPDATE `playerbots_custom_strategy`
   SET `action_line` = 'can cast::Aortic Assault>cast melee::Aortic Assault!85'
 WHERE `name` = 'bloodmage-accursed'
   AND `action_line` = 'can cast::Aortic Assault>cast melee::Aortic Assault!20';

-- 3. Witch Doctor Voodoo : 221 dégâts/s, contre 660 pour Shadowhunting. `Hexfire` est le SEUL sort
--    que le joueur lance vraiment (144 fois), et avec sa retombée `Hexfire Wrath` il vaut 27 % de
--    ses dégâts. Chez nous il était en priorité 24 et n'a produit strictement aucun dégât de la
--    nuit. On le met en tête du bloc mono-cible, devant Shadow Puppets (85), Bad Juju (84) et
--    Malefic Wrath (83).
--      avant : 'can cast::Hexfire>cast::Hexfire!24'
UPDATE `playerbots_custom_strategy`
   SET `action_line` = 'can cast::Hexfire>cast::Hexfire!86'
 WHERE `name` = 'witch-doctor-voodoo' AND `action_line` = 'can cast::Hexfire>cast::Hexfire!24';

-- Non traité volontairement :
--   Tinker Mechanics -- `Mechsuit: Laser Beam` est déjà en priorité 84, le joueur le lance 194 fois,
--   et nos bots n'en ont tiré AUCUN dégât de la nuit. Une priorité haute qui ne produit rien ne se
--   corrige pas en la montant : le sort n'est pas lançable en l'état (forme de mécha requise, sans
--   doute). À instruire, pas à bricoler.
--   Ranger Archery -- les sorts porteurs du joueur (Fire Arrows, Incendiary Shot, Corrosive Shot)
--   ont tous zéro lancer : ce sont des retombées. Ses vrais lancers sont trop peu nombreux dans
--   l'échantillon pour en tirer une rotation. À reprendre avec plus de journaux.

-- ---------------------------------------------------------------------------------------------
-- Deuxième série, même méthode, trois spés de plus (24/09, après-midi).

-- 4. Ranger Brigand : `Quills` est de loin le premier lancer du joueur -- 1 108 fois, 23 % de ses
--    dégâts. Chez nous il était en priorité 29 et ne pesait que 2 %. Le bot n'avait donc plus rien
--    à lancer et passait 39 % du combat à taper en mêlée, le record du serveur. On le place devant
--    Flank (84), Wild Strike (83) et Skullpiercer (82), dont le joueur tire 0 %, 12 % et 2 %.
--      avant : 'can cast::Quills>cast melee::Quills!29'
UPDATE `playerbots_custom_strategy`
   SET `action_line` = 'can cast::Quills>cast melee::Quills!85'
 WHERE `name` = 'ranger-brigand' AND `action_line` = 'can cast::Quills>cast melee::Quills!29';

-- 5. Starcaller Warden : `Umbral Blade`, 640 lancers chez le joueur, était en priorité 29 et ne
--    donnait que 2 % de nos dégâts. On le monte juste sous Astral Blade (83), que le joueur lance
--    523 fois -- l'ordre entre les deux reste celui de l'humain.
--      avant : 'can cast::Umbral Blade>cast melee::Umbral Blade!29'
UPDATE `playerbots_custom_strategy`
   SET `action_line` = 'can cast::Umbral Blade>cast melee::Umbral Blade!82'
 WHERE `name` = 'starcaller-warden' AND `action_line` = 'can cast::Umbral Blade>cast melee::Umbral Blade!29';

-- 6. Runemaster Glyphic : nos bots tirent 71 % de leurs dégâts d'`Elemental Burst`, que les trois
--    joueurs observés ne lancent JAMAIS, et 1 % de `Primordial Pulse`, qu'ils lancent 426 fois.
--    On remonte Primordial Pulse au-dessus d'Elemental Burst (84) et de Glyphic Ruin (83).
--      avant : 'can cast::Primordial Pulse>cast::Primordial Pulse!30'
UPDATE `playerbots_custom_strategy`
   SET `action_line` = 'can cast::Primordial Pulse>cast::Primordial Pulse!85'
 WHERE `name` = 'runemaster-glyphic' AND `action_line` = 'can cast::Primordial Pulse>cast::Primordial Pulse!30';

-- 7. Cultist Corruption : le plus gros décalage trouvé après le Felsworn. Les trois joueurs
--    observés lancent `Obliteration Beam` 674 fois (29 % de leurs dégâts) et
--    `Eldritch Devastation` 455 fois (18 %). Chez nous, priorités 25 et 26 : 1 % et 0 % de nos
--    dégâts sur 56 essais. Nos bots tiraient 56 % de leurs dégâts de `Gaze of C'Thun`, qui est
--    porté par douze joueurs de classes différentes -- un effet d'objet, pas leur rotation.
--    On place les deux sorts du joueur en tête du bloc mono-cible, devant Restore Sanity (81),
--    Gaze of C'Thun (80) et Horrorbolt (79).
--      avant : 'can cast::Obliteration Beam>cast::Obliteration Beam!25'
--              'debuff missing::Eldritch Devastation>cast debuff::Eldritch Devastation!26'
UPDATE `playerbots_custom_strategy`
   SET `action_line` = 'can cast::Obliteration Beam>cast::Obliteration Beam!83'
 WHERE `name` = 'cultist-corruption' AND `action_line` = 'can cast::Obliteration Beam>cast::Obliteration Beam!25';
UPDATE `playerbots_custom_strategy`
   SET `action_line` = 'debuff missing::Eldritch Devastation>cast debuff::Eldritch Devastation!82'
 WHERE `name` = 'cultist-corruption'
   AND `action_line` = 'debuff missing::Eldritch Devastation>cast debuff::Eldritch Devastation!26';

-- ---------------------------------------------------------------------------------------------
-- Les tanks. Ils sont dans l'ensemble mieux alignés que les dps -- leur rotation pèse moins que
-- leur tenue -- mais deux capacités lourdes traînaient en bas de liste.

-- 8. Guardian Vanguard : `Hammer of the Law` vaut 22 % des dégâts du joueur, sa première source,
--    et il était en priorité 26 chez nous. On le met au niveau de Heavy Blow (82) et Pulverize
--    (81), les deux autres capacités que le joueur utilise vraiment.
--      avant : 'can cast::Hammer of the Law>cast melee::Hammer of the Law!26'
UPDATE `playerbots_custom_strategy`
   SET `action_line` = 'can cast::Hammer of the Law>cast melee::Hammer of the Law!83'
 WHERE `name` = 'guardian-vanguard'
   AND `action_line` = 'can cast::Hammer of the Law>cast melee::Hammer of the Law!26';

-- 9. Templar Oathkeeper : `Blade of Faith`, 15 % des dégâts des six joueurs observés, était en
--    priorité 27. On le remonte au-dessus de Righteous Lunge (83), qui reste leur bouche-trou --
--    ils le lancent 2 092 fois, mais il ne leur rapporte que 7 %.
--    Prudence : c'est notre meilleur tank pour la survie du groupe (1,49 mort par essai). On ne
--    touche qu'à cette ligne, et à rien de ce qui concerne la menace ou les soins.
--      avant : 'debuff missing::Blade of Faith>cast debuff::Blade of Faith!27'
UPDATE `playerbots_custom_strategy`
   SET `action_line` = 'debuff missing::Blade of Faith>cast debuff::Blade of Faith!84'
 WHERE `name` = 'templar-oathkeeper'
   AND `action_line` = 'debuff missing::Blade of Faith>cast debuff::Blade of Faith!27';

-- ---------------------------------------------------------------------------------------------
-- Troisième série (24/09, milieu de matinée), après un dépouillement élargi des journaux.

-- 10. Witch Hunter Houndmaster : le cas le plus extrême trouvé. `Shadowblast` est lancé 215 fois
--     par le joueur et vaut 24 % de ses dégâts ; chez nous il était en **priorité 1**, la plus
--     basse possible, et n'a produit aucun dégât sur 52 essais. On le place sous les deux lignes
--     de meute (88 et 87), qui restent la signature de la spé.
--       avant : 'can cast::Shadowblast>cast::Shadowblast!1'
UPDATE `playerbots_custom_strategy`
   SET `action_line` = 'can cast::Shadowblast>cast::Shadowblast!86'
 WHERE `name` = 'witch-hunter-houndmaster' AND `action_line` = 'can cast::Shadowblast>cast::Shadowblast!1';

-- 11. Runemaster Engravement : `Weapon Engraving: Air` est une gravure d'arme -- le joueur ne la
--     pose que 11 fois, mais elle porte 19 % de ses dégâts par ce qu'elle déclenche ensuite. En
--     priorité 14, nos bots ne la posaient jamais : 0 % sur 35 essais. On la remonte au niveau des
--     autres préparations de la spé, Zenith (86) et Runic Tattoos (85), qui sont du même ordre.
--       avant : 'can cast::Weapon Engraving: Air>cast melee::Weapon Engraving: Air!14'
UPDATE `playerbots_custom_strategy`
   SET `action_line` = 'can cast::Weapon Engraving: Air>cast melee::Weapon Engraving: Air!84'
 WHERE `name` = 'runemaster-engravement'
   AND `action_line` = 'can cast::Weapon Engraving: Air>cast melee::Weapon Engraving: Air!14';

-- 12. Ranger Archery : 187 dégâts/s, la spé la plus faible de la classe. `Hunting Shot` est le
--     lancer que les trois joueurs répètent -- 540 fois -- et tout ce qui porte leurs dégâts
--     (Fire Arrows 31 %, Precision Shot 17 %, Incendiary Shot 13 %) a zéro lancer : ce sont ses
--     retombées. En priorité 23, nos bots ne l'ont jamais tiré sur 24 essais.
--       avant : 'can cast::Hunting Shot>cast::Hunting Shot!23'
UPDATE `playerbots_custom_strategy`
   SET `action_line` = 'can cast::Hunting Shot>cast::Hunting Shot!84'
 WHERE `name` = 'ranger-archery' AND `action_line` = 'can cast::Hunting Shot>cast::Hunting Shot!23';

-- 13. Tinker Mechanics : deux capacités peu lancées mais lourdes, `Air Strike` (10 % des dégâts du
--     joueur) et `Piercing Augmentation` (11 %), dormaient en priorités 24 et 8. Aucune des deux
--     n'a produit le moindre dégât chez nous sur 62 essais.
--     `Mechsuit: Laser Beam` reste en 84 et reste muet : le joueur le lance 285 fois, nos bots
--     zéro, alors que sa priorité est déjà haute. Le sort n'est donc pas lançable en l'état, et
--     monter une priorité n'y changera rien -- à instruire séparément.
--       avant : 'can cast::Air Strike>cast::Air Strike!24'
--               'can cast::Piercing Augmentation>cast::Piercing Augmentation!8'
UPDATE `playerbots_custom_strategy`
   SET `action_line` = 'can cast::Air Strike>cast::Air Strike!83'
 WHERE `name` = 'tinker-mechanics' AND `action_line` = 'can cast::Air Strike>cast::Air Strike!24';
UPDATE `playerbots_custom_strategy`
   SET `action_line` = 'can cast::Piercing Augmentation>cast::Piercing Augmentation!82'
 WHERE `name` = 'tinker-mechanics'
   AND `action_line` = 'can cast::Piercing Augmentation>cast::Piercing Augmentation!8';
