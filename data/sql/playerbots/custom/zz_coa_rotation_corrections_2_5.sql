-- CoA Bots 2.5: a player's dungeon report (Steamvault, Shadow Labyrinth, 16/10) and player tips from the spec pages, on
-- top of zz_coa_rotation_corrections_2_4.sql. Default rotations only (owner 0). Rows are chosen by their CONTENT (name +
-- owner 0 + action_line), never by id or idx. Re-runnable: once applied it changes nothing. Rows with owner <> 0 are
-- never touched. Rotations are cached per process (CustomStrategy::actionLinesCache): the change shows after a restart.

DROP PROCEDURE IF EXISTS coa25_add;
DELIMITER $$
CREATE PROCEDURE coa25_add(IN p_name VARCHAR(255), IN p_line TEXT)
BEGIN
  IF EXISTS (SELECT 1 FROM playerbots_custom_strategy WHERE owner = 0 AND name = p_name)
     AND NOT EXISTS (SELECT 1 FROM playerbots_custom_strategy WHERE owner = 0 AND name = p_name AND action_line = p_line) THEN
    INSERT INTO playerbots_custom_strategy (name, idx, owner, action_line)
    SELECT p_name, MAX(idx) + 1, 0, p_line FROM playerbots_custom_strategy WHERE owner = 0 AND name = p_name;
  END IF;
END$$
DELIMITER ;

DROP PROCEDURE IF EXISTS coa25_set;
DELIMITER $$
CREATE PROCEDURE coa25_set(IN p_name VARCHAR(255), IN p_old TEXT, IN p_new TEXT)
BEGIN
  IF NOT EXISTS (SELECT 1 FROM playerbots_custom_strategy WHERE owner = 0 AND name = p_name AND action_line = p_new) THEN
    UPDATE playerbots_custom_strategy SET action_line = p_new WHERE owner = 0 AND name = p_name AND action_line = p_old;
  ELSE
    DELETE FROM playerbots_custom_strategy WHERE owner = 0 AND name = p_name AND action_line = p_old;
  END IF;
END$$
DELIMITER ;

-- 1. Cultist Dreadnought. Test of Pride and Twilight Shieldtoss reach 30 yd but sat on melee rows: every refusal came from
--    out of melee range. Sanity Tap, its mana return, waited at the bottom of the list on a melee row and was never cast
--    while the tank spent a fifth of its fights under 10% mana.
CALL coa25_set('cultist-dreadnought', 'lose aggro>cast melee::Test of Pride!93', 'lose aggro>cast::Test of Pride!93');
CALL coa25_set('cultist-dreadnought', 'medium aoe>cast melee::Twilight Shieldtoss!88', 'medium aoe>cast::Twilight Shieldtoss!88');
CALL coa25_set('cultist-dreadnought', 'can cast::Twilight Shieldtoss>cast melee::Twilight Shieldtoss!87', 'can cast::Twilight Shieldtoss>cast::Twilight Shieldtoss!87');
CALL coa25_set('cultist-dreadnought', 'can cast::Sanity Tap>cast melee::Sanity Tap!6', 'low mana>cast::Sanity Tap!89');

-- 2. Tinker Invention (healer): Build: Rusthound is a 6 s cast; in fights the healer built it twice as often as it healed
--    before the tank died. Lock picking, the sawmill and the parachute have no place in a fight either.
DELETE FROM `playerbots_custom_strategy`
 WHERE `owner` = 0 AND `name` = 'tinker-invention'
   AND `action_line` IN ('can cast::Build: Rusthound>cast::Build: Rusthound!19', 'can cast::''Pick Lock''>cast::''Pick Lock''!8',
                         'can cast::Build: Portable Sawmill>cast::Build: Portable Sawmill!7',
                         'can cast::Parachute Pack>cast::Parachute Pack!10');

-- 3. Knight of Xoroth Defiance (player tip): Suffuse spends 6 stacks of Demonfire instead of waiting for half health; Melt
--    too, on a single target; Burning Rage and Demon Heart come up on cooldown, one at a time, instead of below the fillers.
CALL coa25_set('knight-of-xoroth-defiance', 'medium health>cast heal::Suffuse!85', 'aura stacks::Demonfire,6>cast buff::Suffuse!86');
CALL coa25_set('knight-of-xoroth-defiance', 'can cast::Melt>cast melee::Melt!11', 'aura stacks::Demonfire,6>cast melee::Melt!82.5');
CALL coa25_set('knight-of-xoroth-defiance', 'buff missing::Burning Rage>cast buff::Burning Rage!29', 'buff missing::Burning Rage>cast buff::Burning Rage!80');
CALL coa25_set('knight-of-xoroth-defiance', 'can cast::Demon Heart>cast melee::Demon Heart!21', 'no aura::Burning Rage>cast::Demon Heart!79');

-- 4. Sun Cleric Blessings (player tip). Holy Form sat under Gavel of Light, a filler always castable: never cast. Circle of
--    Valor when several members are hurt; Blessing of Light on a member about to die; Chosen of the Light when the healer
--    itself is low; Vow of Light kept up, Vow of Grace from half mana; Dawn spent at 20 stacks.
CALL coa25_set('sun-cleric-blessings', 'buff missing::Holy Form>cast buff::Holy Form!22', 'buff missing::Holy Form>cast buff::Holy Form!31.5');
CALL coa25_set('sun-cleric-blessings', 'can cast::Circle of Valor>cast::Circle of Valor!14', 'medium group heal setting>cast::Circle of Valor!87.5');
CALL coa25_add('sun-cleric-blessings', 'party member critical health>cast heal party::Blessing of Light!94');
CALL coa25_add('sun-cleric-blessings', 'low health>cast buff::Chosen of the Light!29');
CALL coa25_add('sun-cleric-blessings', 'medium mana>cast buff::Vow of Grace!26.6');
CALL coa25_add('sun-cleric-blessings', 'buff missing::Vow of Light>cast buff::Vow of Light!26.5');
CALL coa25_add('sun-cleric-blessings', 'aura stacks::Dawn,20>cast::Dawn!84.5');

-- 5. Primalist Mountain King (player tip): Ursoc's Bellow sat under Quake and Primal Rush, always castable: never cast.
CALL coa25_set('primalist-mountain-king', 'debuff missing::Ursoc''s Bellow>cast melee::Ursoc''s Bellow!31', 'debuff missing::Ursoc''s Bellow>cast melee::Ursoc''s Bellow!85.5');

DROP PROCEDURE IF EXISTS coa25_add;
DROP PROCEDURE IF EXISTS coa25_set;
