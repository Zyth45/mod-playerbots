-- CoA Bots 1.7: rotation fixes for the weakest damage specs at the boss bench
-- (Ranger, Tinker, Bloodmage, Chronomancer, Stormbringer), from the night bench of 01/10/2026.
-- Played on the dev server since 01/10. Applied once by Installer-Bots (coa_bots_installed).

START TRANSACTION;

CREATE TABLE IF NOT EXISTS playerbots_custom_strategy_bak_v17 AS
SELECT * FROM playerbots_custom_strategy
WHERE name IN ('ranger-archery','ranger-farstrider','ranger-brigand','tinker-demolition','tinker-mechanics',
               'bloodmage-accursed','bloodmage-sanguine','chronomancer-artificer',
               'stormbringer-lightning','stormbringer-wind','stormbringer-maelstrom');

-- Re-runnable: the added lines (idx 101-103) are removed first, the UPDATEs only match the 1.6 text.
DELETE FROM playerbots_custom_strategy WHERE owner = 0 AND idx BETWEEN 101 AND 103 AND name IN ('bloodmage-accursed', 'ranger-archery', 'ranger-brigand', 'ranger-farstrider', 'stormbringer-lightning', 'stormbringer-maelstrom', 'stormbringer-wind');


-- -------------------------------------------------------------------------------------
-- RANGER / ARCHERY  (NUC : 271 DPS median, 40 runs ; Auto Shot 34 % des degats,
-- Precision Shot 0 lancer en 40 runs, Skullpiercer tire a 0-1 Advantage : 872 de moyenne
-- contre 3333 chez les joueurs)
-- Schema voulu : depensier sous Advantage >= 4 (85) > generateurs (84/82) > depensier de
-- secours (70) > Brutal Shot. Precision Shot (portee MINIMALE 8 m) ne partira qu'une fois
-- le placement corrige (voir le rapport, correctif de code n°1).
-- -------------------------------------------------------------------------------------
UPDATE playerbots_custom_strategy SET action_line = 'can cast::Auto Shot>cast::Auto Shot!6'
 WHERE name = 'ranger-archery' AND action_line = 'can cast::Auto Shot>cast::Auto Shot!80';
UPDATE playerbots_custom_strategy SET action_line = 'aura stacks::Advantage,4>cast::Skullpiercer!85'
 WHERE name = 'ranger-archery' AND action_line = 'can cast::Skullpiercer>cast::Skullpiercer!79';
UPDATE playerbots_custom_strategy SET action_line = 'aura stacks::Advantage,3>cast::Precision Shot!83'
 WHERE name = 'ranger-archery' AND action_line = 'can cast::Precision Shot>cast::Precision Shot!81';
UPDATE playerbots_custom_strategy SET action_line = 'can cast::Brutal Shot>cast::Brutal Shot!80'
 WHERE name = 'ranger-archery' AND action_line = 'can cast::Brutal Shot>cast::Brutal Shot!26';
INSERT INTO playerbots_custom_strategy (name, idx, owner, action_line) VALUES
 ('ranger-archery', 101, 0, 'can cast::Instinct>cast::Instinct!89'),
 ('ranger-archery', 102, 0, 'can cast::Incendiary Shot>cast::Incendiary Shot!86'),
 ('ranger-archery', 103, 0, 'can cast::Skullpiercer>cast::Skullpiercer!70');

-- RANGER / FARSTRIDER  (4 runs seulement : faible confiance, meme logique qu'Archery)
UPDATE playerbots_custom_strategy SET action_line = 'aura stacks::Advantage,4>cast::Skullpiercer!85'
 WHERE name = 'ranger-farstrider' AND action_line = 'can cast::Skullpiercer>cast::Skullpiercer!80';
UPDATE playerbots_custom_strategy SET action_line = 'aura stacks::Advantage,4>cast::Woodland Arrow!84'
 WHERE name = 'ranger-farstrider' AND action_line = 'can cast::Woodland Arrow>cast::Woodland Arrow!18';
INSERT INTO playerbots_custom_strategy (name, idx, owner, action_line) VALUES
 ('ranger-farstrider', 101, 0, 'can cast::Instinct>cast::Instinct!89'),
 ('ranger-farstrider', 102, 0, 'can cast::Skullpiercer>cast::Skullpiercer!70');

-- RANGER / BRIGAND  (416 DPS median, 96 runs ; 60 % du total en coups blancs ; Quills a
-- 157 de moyenne contre 1953 chez les joueurs = lance a vide d'Advantage)
UPDATE playerbots_custom_strategy SET action_line = 'aura stacks::Advantage,4>cast melee::Quills!87'
 WHERE name = 'ranger-brigand' AND action_line = 'can cast::Quills>cast melee::Quills!84';
UPDATE playerbots_custom_strategy SET action_line = 'aura stacks::Advantage,4>cast melee::Skullpiercer!86'
 WHERE name = 'ranger-brigand' AND action_line = 'can cast::Skullpiercer>cast melee::Skullpiercer!83';
INSERT INTO playerbots_custom_strategy (name, idx, owner, action_line) VALUES
 ('ranger-brigand', 101, 0, 'can cast::Instinct>cast::Instinct!89'),
 ('ranger-brigand', 102, 0, 'can cast::Quills>cast melee::Quills!40');

-- -------------------------------------------------------------------------------------
-- TINKER / DEMOLITION  (430 DPS median, 55 runs ; Air Strike 0,1 lancer/run a la
-- priorite 24 alors que c'est 17 % des degats des joueurs ; Rocket Launcher (80) toujours
-- masque par Scrap Shot (81, toujours lancable) ; Rockadier (23) idem)
-- -------------------------------------------------------------------------------------
UPDATE playerbots_custom_strategy SET action_line = 'can cast::Air Strike>cast::Air Strike!89'
 WHERE name = 'tinker-demolition' AND action_line = 'can cast::Air Strike>cast::Air Strike!24';
UPDATE playerbots_custom_strategy SET action_line = 'buff missing::Rockadier>cast buff::Rockadier!88'
 WHERE name = 'tinker-demolition' AND action_line = 'buff missing::Rockadier>cast buff::Rockadier!23';
UPDATE playerbots_custom_strategy SET action_line = 'can cast::Rocket Launcher>cast::Rocket Launcher!84'
 WHERE name = 'tinker-demolition' AND action_line = 'can cast::Rocket Launcher>cast::Rocket Launcher!80';
UPDATE playerbots_custom_strategy SET action_line = 'can cast::Scrap Shot>cast::Scrap Shot!50'
 WHERE name = 'tinker-demolition' AND action_line = 'can cast::Scrap Shot>cast::Scrap Shot!81';
-- Explosive Augmentation (9,8 % chez les joueurs) a la place de Tracer (2,2 % chez nos bots)
UPDATE playerbots_custom_strategy SET action_line = 'can cast::Explosive Augmentation>cast::Explosive Augmentation!28'
 WHERE name = 'tinker-demolition' AND action_line = 'can cast::Explosive Augmentation>cast::Explosive Augmentation!10';
UPDATE playerbots_custom_strategy SET action_line = 'can cast::Tracer Augmentation>cast::Tracer Augmentation!9'
 WHERE name = 'tinker-demolition' AND action_line = 'can cast::Tracer Augmentation>cast::Tracer Augmentation!28';

-- TINKER / MECHANICS  (337 DPS median, 61 runs)
UPDATE playerbots_custom_strategy SET action_line = 'can cast::Air Strike>cast::Air Strike!89'
 WHERE name = 'tinker-mechanics' AND action_line = 'can cast::Air Strike>cast::Air Strike!83';
UPDATE playerbots_custom_strategy SET action_line = 'can cast::Explosive Augmentation>cast::Explosive Augmentation!31'
 WHERE name = 'tinker-mechanics' AND action_line = 'can cast::Explosive Augmentation>cast::Explosive Augmentation!10';
UPDATE playerbots_custom_strategy SET action_line = 'can cast::Piercing Augmentation>cast::Piercing Augmentation!9'
 WHERE name = 'tinker-mechanics' AND action_line = 'can cast::Piercing Augmentation>cast::Piercing Augmentation!30';

-- -------------------------------------------------------------------------------------
-- BLOODMAGE / ACCURSED  (416 DPS median, 75 runs ; Aortic Assault 7 coups/run a 197
-- contre ~25 coups par lancer a 1228 chez les joueurs : il faut etre en Cursed Form ;
-- Crimson Maw (2301 de moyenne chez les joueurs) enterre a 30)
-- -------------------------------------------------------------------------------------
INSERT INTO playerbots_custom_strategy (name, idx, owner, action_line) VALUES
 ('bloodmage-accursed', 101, 0, 'has aura::Accursed Form>cast melee::Aortic Assault!87');
UPDATE playerbots_custom_strategy SET action_line = 'can cast::Crimson Maw>cast melee::Crimson Maw!83'
 WHERE name = 'bloodmage-accursed' AND action_line = 'can cast::Crimson Maw>cast melee::Crimson Maw!30';

-- BLOODMAGE / SANGUINE  (335 DPS median, 65 runs ; Valanar's Vengeance 5245 de moyenne
-- mais 2,2 lancers/run dans 15 % des runs : Vampiric Fang (96) mange les charges de Thirst
-- que Valanar (82) attend ; Bloodmoon Blast (60, 7 % de vie par lancer) passe devant tout)
UPDATE playerbots_custom_strategy SET action_line = 'aura stacks::Thirst,3>cast::Valanar''s Vengeance!95'
 WHERE name = 'bloodmage-sanguine' AND action_line = 'aura stacks::Thirst,3>cast::Valanar''s Vengeance!82';
UPDATE playerbots_custom_strategy SET action_line = 'aura stacks::Thirst,5>cast::Vampiric Fang!70'
 WHERE name = 'bloodmage-sanguine' AND action_line = 'aura stacks::Thirst,5>cast::Vampiric Fang!96';

-- -------------------------------------------------------------------------------------
-- CHRONOMANCER / ARTIFICER  (328 DPS median, 72 runs ; "can cast::Wand" a 77 : la baguette
-- passe devant toutes les lignes en dessous, 46,8 coups de baguette par run)
-- -------------------------------------------------------------------------------------
UPDATE playerbots_custom_strategy SET action_line = 'can cast::Wand>cast::Wand!5'
 WHERE name = 'chronomancer-artificer' AND action_line = 'can cast::Wand>cast::Wand!77';

-- -------------------------------------------------------------------------------------
-- STORMBRINGER / LIGHTNING  (636 DPS median, 36 runs ; Arm of Thorim lance a Static bas :
-- 3574 de moyenne contre 19053 chez les joueurs ; Discharge (rend du mana, 510 lancers chez
-- les joueurs) a la priorite 2 ; 26 coups de baguette par run = a sec de mana)
-- -------------------------------------------------------------------------------------
UPDATE playerbots_custom_strategy SET action_line = 'can cast::Charge>cast::Charge!88'
 WHERE name = 'stormbringer-lightning' AND action_line = 'can cast::Charge>cast::Charge!80';
UPDATE playerbots_custom_strategy SET action_line = 'aura stacks::Static,60>cast::Arm of Thorim!87'
 WHERE name = 'stormbringer-lightning' AND action_line = 'can cast::Arm of Thorim>cast::Arm of Thorim!79';
UPDATE playerbots_custom_strategy SET action_line = 'aura stacks::Static,100>cast::Call Lightning!84'
 WHERE name = 'stormbringer-lightning' AND action_line = 'can cast::Call Lightning>cast::Call Lightning!76';
INSERT INTO playerbots_custom_strategy (name, idx, owner, action_line) VALUES
 ('stormbringer-lightning', 101, 0, 'medium mana>cast::Discharge!89');

-- STORMBRINGER / WIND  (352 DPS median, 64 runs ; Aeroblast ABSENT de la rotation (lance
-- seulement par le classifieur), Gale a 19, 27 coups de baguette par run)
UPDATE playerbots_custom_strategy SET action_line = 'can cast::Gale>cast::Gale!84'
 WHERE name = 'stormbringer-wind' AND action_line = 'can cast::Gale>cast::Gale!19';
INSERT INTO playerbots_custom_strategy (name, idx, owner, action_line) VALUES
 ('stormbringer-wind', 101, 0, 'aura stacks::Static,50>cast::Aeroblast!86'),
 ('stormbringer-wind', 102, 0, 'aura stacks::Static,70>cast::Updraft!85'),
 ('stormbringer-wind', 103, 0, 'medium mana>cast::Discharge!89');

-- STORMBRINGER / MAELSTROM  (388 DPS median, 41 runs ; Brine (63 % des degats) et
-- Torrential Wrath (12087 de moyenne chez les joueurs, 0,4 lancer/run chez nous) absents
-- de la rotation ; Charge (+100 Static) enterree a 23)
INSERT INTO playerbots_custom_strategy (name, idx, owner, action_line) VALUES
 ('stormbringer-maelstrom', 101, 0, 'aura stacks::Static,40>cast::Torrential Wrath!88'),
 ('stormbringer-maelstrom', 102, 0, 'can cast::Brine>cast::Brine!80'),
 ('stormbringer-maelstrom', 103, 0, 'medium mana>cast::Discharge!89');
UPDATE playerbots_custom_strategy SET action_line = 'can cast::Charge>cast::Charge!87'
 WHERE name = 'stormbringer-maelstrom' AND action_line = 'can cast::Charge>cast::Charge!23';

COMMIT;
