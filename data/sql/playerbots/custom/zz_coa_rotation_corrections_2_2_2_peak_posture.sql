-- CoA Bots: the Assume Peak Posture row of the three Guardian rotations (owner 0).
--
-- Rows are chosen by their CONTENT (name + owner 0 + action_line), never by id or idx: the ids differ
-- from one installation to the next. Re-runnable: once applied it changes nothing. Rows with
-- owner <> 0 are never touched. The file name sorts after zz_coa_rotation_corrections_2_1_1_b.sql, so it
-- applies on top of every earlier rotation file (the rows come from the 1.9 file). Rotations are cached per process
-- (CustomStrategy::actionLinesCache): the change shows after a worldserver restart.
--
-- Assume Peak Posture (500344, Guardian class spell from level 60) is cosmetic: a larger model, a dummy,
-- and guards nearby clap (500345 Envy every 5 min). 1 s cast, 60 s cooldown, no duration limit, and
-- moving, turning or jumping ends it (AuraInterruptFlags 0x2080038; tooltip: "moving will end the
-- effect"). As a 'buff missing' row the bot stopped in combat to cast it every time the cooldown was
-- over and lost it at its next step.

-- 1. guardian-gladiator idx 15
DELETE FROM `playerbots_custom_strategy`
 WHERE `owner` = 0
   AND `name` = 'guardian-gladiator'
   AND `action_line` = 'buff missing::Assume Peak Posture>cast buff::Assume Peak Posture!29';

-- 2. guardian-inspiration idx 21
DELETE FROM `playerbots_custom_strategy`
 WHERE `owner` = 0
   AND `name` = 'guardian-inspiration'
   AND `action_line` = 'buff missing::Assume Peak Posture>cast buff::Assume Peak Posture!27';

-- 3. guardian-vanguard idx 12
DELETE FROM `playerbots_custom_strategy`
 WHERE `owner` = 0
   AND `name` = 'guardian-vanguard'
   AND `action_line` = 'buff missing::Assume Peak Posture>cast buff::Assume Peak Posture!29';
