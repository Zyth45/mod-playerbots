-- CoA Bots 2.1: default rotation (owner 0) of primalist-grovekeeper, on top of zz_coa_rotation_corrections_2_0.sql.
-- Applied once by Installer-Bots (coa_bots_installed). Re-runnable: the same result every time. Rows with
-- owner <> 0 and the 'say' strategy are never touched. A wrong result (count, checksum, apostrophes) aborts
-- the transaction.
-- Rows: 40 in 1 spec. Checksum SUM(CRC32(CONCAT_WS('|',name,idx,owner,action_line))) = 97562405228.
-- Why (players' report on 2.0, checked against real players' combat logs, mythic+ and raids 07-08/2026,
-- 12 Grovekeepers, about 44,000 casts): the bots fought in melee but kept channelling Geode Barrage
-- (2.5 s), hardcast Stoneshard and hardly used Seismic Wave or Earthquake. Real Grovekeepers cast Wildclaw
-- (51% of casts), Geode Barrage (11%, a rage builder they channel when short of Rage, mostly on one target;
-- 2 to 8% for most players), Earthquake (11%, 15% of casts on packs), Seismic Wave (8%, on cooldown, 2 to 5
-- targets), Hand of the Earthmother (5%), Seismic Crash (3%), Therazane's Rage and Grove Guardian; none cast
-- Stoneshard and Spirit Charge is nearly unused. Seismic Wave sat under Wildclaw (always castable with Rage),
-- so it was skipped: it now comes first on its cooldown, Earthquake and Seismic Spike open packs, Geode
-- Barrage only fills when nothing else can be cast (no Rage), Stoneshard goes, and the cooldowns that deal
-- 5 to 14% of real players' damage (Grove Guardian, Neptulon's Wrath) or give Rage (Therazane's Rage) come
-- before everything.

DROP PROCEDURE IF EXISTS coa21_check;
DELIMITER $$
CREATE PROCEDURE coa21_check()
BEGIN
  DECLARE n BIGINT DEFAULT 0;
  DECLARE h BIGINT DEFAULT 0;
  DECLARE a BIGINT DEFAULT 0;
  SELECT COUNT(*), COALESCE(SUM(CRC32(CONCAT_WS('|', name, idx, owner, action_line))), 0)
    INTO n, h FROM playerbots_custom_strategy
   WHERE owner = 0 AND name IN ('primalist-grovekeeper');
  SELECT COUNT(*) INTO a FROM playerbots_custom_strategy WHERE owner = 0 AND name IN ('primalist-grovekeeper')
     AND action_line LIKE '%''%';
  IF n <> 40 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'CoA Bots 2.1 rotations: wrong row count, nothing was changed';
  END IF;
  IF a <> 4 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'CoA Bots 2.1 rotations: apostrophes lost (character set), nothing was changed';
  END IF;
  IF h <> 97562405228 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'CoA Bots 2.1 rotations: wrong checksum, nothing was changed';
  END IF;
END$$
DELIMITER ;

-- A copy of the default rotations as they were before the first run of this file.
CREATE TABLE IF NOT EXISTS playerbots_custom_strategy_bak_v21 AS
SELECT * FROM playerbots_custom_strategy WHERE owner = 0;

START TRANSACTION;

DELETE FROM playerbots_custom_strategy WHERE owner = 0 AND name IN ('primalist-grovekeeper');

INSERT INTO playerbots_custom_strategy (name, idx, owner, action_line) VALUES
 ('primalist-grovekeeper', 1, 0, 'can cast::Therazane''s Rage>cast::Therazane''s Rage!92'),
 ('primalist-grovekeeper', 2, 0, 'can cast::Grove Guardian>cast::Grove Guardian!91'),
 ('primalist-grovekeeper', 3, 0, 'can cast::Neptulon''s Wrath>cast::Neptulon''s Wrath!90'),
 ('primalist-grovekeeper', 4, 0, 'buff missing::Boon of the Bear>cast buff::Boon of the Bear!89'),
 ('primalist-grovekeeper', 5, 0, 'medium aoe>cast::Seismic Wave!88'),
 ('primalist-grovekeeper', 6, 0, 'medium aoe>cast::Earthquake!87'),
 ('primalist-grovekeeper', 7, 0, 'medium aoe>cast::Seismic Spike!86'),
 ('primalist-grovekeeper', 8, 0, 'medium aoe>cast melee::Wildclaw!85'),
 ('primalist-grovekeeper', 9, 0, 'can cast::Seismic Wave>cast::Seismic Wave!84'),
 ('primalist-grovekeeper', 10, 0, 'debuff missing::Seismic Crash>cast debuff::Seismic Crash!83'),
 ('primalist-grovekeeper', 11, 0, 'medium health>cast heal::Hand of the Earthmother!82'),
 ('primalist-grovekeeper', 12, 0, 'can cast::Wildclaw>cast melee::Wildclaw!81'),
 ('primalist-grovekeeper', 13, 0, 'debuff missing::Earthquake>cast debuff::Earthquake!80'),
 ('primalist-grovekeeper', 14, 0, 'can cast::Ancient of War>cast::Ancient of War!78'),
 ('primalist-grovekeeper', 15, 0, 'can cast::Sacred Grove>cast::Sacred Grove!77'),
 ('primalist-grovekeeper', 16, 0, 'can cast::Primal Awakening>cast::Primal Awakening!76'),
 ('primalist-grovekeeper', 17, 0, 'buff missing::Primal Convergence>cast buff::Primal Convergence!75'),
 ('primalist-grovekeeper', 18, 0, 'can cast::Primal Instinct>cast::Primal Instinct!74'),
 ('primalist-grovekeeper', 19, 0, 'can cast::Seismic Spike>cast::Seismic Spike!73'),
 ('primalist-grovekeeper', 20, 0, 'can cast::Geode Barrage>cast::Geode Barrage!40'),
 ('primalist-grovekeeper', 21, 0, 'can cast::Primal Rush>cast::Primal Rush!30'),
 ('primalist-grovekeeper', 22, 0, 'buff missing::Boulder Dash>cast buff::Boulder Dash!30'),
 ('primalist-grovekeeper', 23, 0, 'buff missing::Greater Earthen Endurance>cast buff party::Greater Earthen Endurance!28'),
 ('primalist-grovekeeper', 24, 0, 'buff missing::Greater Grove Instinct>cast buff party::Greater Grove Instinct!27'),
 ('primalist-grovekeeper', 25, 0, 'buff missing::Greater Essence of Nature>cast buff party::Greater Essence of Nature!26'),
 ('primalist-grovekeeper', 26, 0, 'buff missing::Greater Primal Instinct>cast buff party::Greater Primal Instinct!25'),
 ('primalist-grovekeeper', 27, 0, 'can cast::Protective Roar>cast::Protective Roar!22'),
 ('primalist-grovekeeper', 28, 0, 'can cast::Ancient of Lore>cast::Ancient of Lore!21'),
 ('primalist-grovekeeper', 29, 0, 'buff missing::Flourishing Growth>cast buff party::Flourishing Growth!20'),
 ('primalist-grovekeeper', 30, 0, 'can cast::Essence of Nature>cast::Essence of Nature!17'),
 ('primalist-grovekeeper', 31, 0, 'can cast::Earthen Endurance>cast::Earthen Endurance!16'),
 ('primalist-grovekeeper', 32, 0, 'can cast::Soothing Touch>cast::Soothing Touch!15'),
 ('primalist-grovekeeper', 33, 0, 'can cast::Ursoc''s Bellow>cast::Ursoc''s Bellow!14'),
 ('primalist-grovekeeper', 34, 0, 'can cast::Nature''s Call>cast::Nature''s Call!13'),
 ('primalist-grovekeeper', 35, 0, 'can cast::Spirit Charge>cast::Spirit Charge!12'),
 ('primalist-grovekeeper', 36, 0, 'can cast::Return to Life>cast::Return to Life!11'),
 ('primalist-grovekeeper', 37, 0, 'can cast::Grove Instinct>cast::Grove Instinct!7'),
 ('primalist-grovekeeper', 38, 0, 'can cast::Throw>cast::Throw!6'),
 ('primalist-grovekeeper', 39, 0, 'can cast::Shoot>cast::Shoot!2'),
 ('primalist-grovekeeper', 40, 0, 'can cast::Wand>cast::Wand!1');

CALL coa21_check();
COMMIT;
DROP PROCEDURE coa21_check;
