-- CoA Bots 2.3: player reports from the specs site (kuribo), on top of zz_coa_rotation_corrections_2_2_2_*.sql.
-- Default rotations only (owner 0). Rows are chosen by their CONTENT (name + owner 0 + action_line), never by id or
-- idx. Re-runnable: once applied it changes nothing. Rows with owner <> 0 are never touched. Rotations are cached per
-- process (CustomStrategy::actionLinesCache): the change shows after a worldserver restart.

DROP PROCEDURE IF EXISTS coa23_add;
DELIMITER $$
CREATE PROCEDURE coa23_add(IN p_name VARCHAR(255), IN p_line TEXT)
BEGIN
  IF EXISTS (SELECT 1 FROM playerbots_custom_strategy WHERE owner = 0 AND name = p_name)
     AND NOT EXISTS (SELECT 1 FROM playerbots_custom_strategy WHERE owner = 0 AND name = p_name AND action_line = p_line) THEN
    INSERT INTO playerbots_custom_strategy (name, idx, owner, action_line)
    SELECT p_name, MAX(idx) + 1, 0, p_line FROM playerbots_custom_strategy WHERE owner = 0 AND name = p_name;
  END IF;
END$$
DELIMITER ;

-- 1. Witch Doctor Shadowhunting: Frenzied Spirits and Spirit Eclipse spend 5 stacks of Spirit, not 3 or on low
--    health; no Hex of Malice and no Malefic Wrath (Spirit Glaive turns into Umbral Glaive on its proc by itself, and
--    that one refreshes the hex); Shadow Avatar comes up first instead of after every filler.
DELETE FROM `playerbots_custom_strategy`
 WHERE `owner` = 0 AND `name` = 'witch-doctor-shadowhunting'
   AND `action_line` IN ('debuff missing::Hex of Malice>cast debuff::Hex of Malice!84',
                         'debuff missing::Hex of Malice>cast debuff::Hex of Malice!3',
                         'can cast::Malefic Wrath>cast::Malefic Wrath!60',
                         'medium health>cast buff::Frenzied Spirits!66',
                         'buff missing::Frenzied Spirits>cast buff::Frenzied Spirits!27',
                         'can cast::Spirit Eclipse>cast::Spirit Eclipse!52');
UPDATE `playerbots_custom_strategy` SET `action_line` = 'aura stacks::Spirit,5>cast::Spirit Eclipse!78'
 WHERE `owner` = 0 AND `name` = 'witch-doctor-shadowhunting'
   AND `action_line` = 'aura stacks::Spirit,3>cast::Spirit Eclipse!78';
UPDATE `playerbots_custom_strategy` SET `action_line` = 'buff missing::Shadow Avatar>cast buff::Shadow Avatar!86'
 WHERE `owner` = 0 AND `name` = 'witch-doctor-shadowhunting'
   AND `action_line` = 'buff missing::Shadow Avatar>cast buff::Shadow Avatar!13';
CALL coa23_add('witch-doctor-shadowhunting', 'aura stacks::Spirit,5>cast buff::Frenzied Spirits!79');

-- 2. Primalist Mountain King (tank): the defensive cooldowns wait for a dangerous moment instead of going out on
--    cooldown all together: Earthen Avatar under low health, then Bearskin and Primal Convergence under critical
--    health; Ursoc's Bellow (enemies' attack power) kept up on the target.
UPDATE `playerbots_custom_strategy` SET `action_line` = 'low health>cast buff::Earthen Avatar!93'
 WHERE `owner` = 0 AND `name` = 'primalist-mountain-king'
   AND `action_line` = 'buff missing::Earthen Avatar>cast buff::Earthen Avatar!24';
UPDATE `playerbots_custom_strategy` SET `action_line` = 'critical health>cast buff::Primal Convergence!92'
 WHERE `owner` = 0 AND `name` = 'primalist-mountain-king'
   AND `action_line` = 'buff missing::Primal Convergence>cast buff::Primal Convergence!21';
UPDATE `playerbots_custom_strategy` SET `action_line` = 'debuff missing::Ursoc''s Bellow>cast melee::Ursoc''s Bellow!31'
 WHERE `owner` = 0 AND `name` = 'primalist-mountain-king'
   AND `action_line` = 'can cast::Ursoc''s Bellow>cast melee::Ursoc''s Bellow!16';
CALL coa23_add('primalist-mountain-king', 'critical health>cast buff::Bearskin!91');

-- 3. Venomancer Fortitude (tank): Weakening Venom (lowers the attack power of what it hits) instead of Adrenal
--    Venom, and Lifeblood under low health.
UPDATE `playerbots_custom_strategy` SET `action_line` = 'buff missing::Weakening Venom>cast buff::Weakening Venom!22'
 WHERE `owner` = 0 AND `name` = 'venomancer-fortitude'
   AND `action_line` = 'buff missing::Adrenal Venom>cast buff::Adrenal Venom!22';
CALL coa23_add('venomancer-fortitude', 'low health>cast::Lifeblood!92');

DROP PROCEDURE IF EXISTS coa23_add;
