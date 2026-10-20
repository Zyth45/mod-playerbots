-- CoA Bots: three rows of the Pyromancer Flameweaving (healer) default rotation (owner 0).
-- Found in four headless Deadmines runs (mod-dungeon-clear test driver, level 21 group, 10/10/2026).
--
-- Rows are chosen by their CONTENT (name + owner 0 + action_line), never by id or idx: the ids differ
-- from one installation to the next. Re-runnable: once applied it changes nothing. Rows with
-- owner <> 0 are never touched. The file name sorts after zz_coa_rotation_corrections_1_9.sql
-- ('.' < '_'), so it applies on top of the 1.9 rows. Rotations are cached per process
-- (CustomStrategy::actionLinesCache): the change shows after a worldserver restart.
--
-- 1. idx 19 'can cast::Dragon Leap>cast::Dragon Leap!22'
--    Dragon Leap (806611, ranks 807615-807618) is a leap onto the target (JUMP_DEST, 35 yd, 30 s
--    cooldown, 14% base mana). As a 'can cast' filler the healer leapt every time the cooldown ended,
--    away from the tank and into the pack: 20 leaps in one run.
DELETE FROM `playerbots_custom_strategy`
 WHERE `owner` = 0
   AND `name` = 'pyromancer-flameweaving'
   AND `action_line` = 'can cast::Dragon Leap>cast::Dragon Leap!22';

-- 2. idx 30 'buff missing::Flame Step>cast buff::Flame Step!11'
--    Flame Step (801911) is a scripted forward dash of about 15 yd (StartDash, core
--    AscensionPyromancerAbilities.cpp), 20 s cooldown, 14% base mana. Read as a buff, it was recast
--    every 20 s: 27 dashes in one run. In the Gilnid room one dash took the healer into aggro range
--    of four adds nobody had pulled yet; all four were on her within 3 s.
DELETE FROM `playerbots_custom_strategy`
 WHERE `owner` = 0
   AND `name` = 'pyromancer-flameweaving'
   AND `action_line` = 'buff missing::Flame Step>cast buff::Flame Step!11';

-- 3. idx 10 'low mana>cast heal::Kindle!83' -> 'low mana>cast heal party::Kindle!83'
--    'cast heal::' heals the caster (CoaCastHealAction). The guide line behind this row is "Mana low:
--    lean on Kindle", that is, keep healing the group with the cheap heal. As a self heal it came up
--    217 times in one run and was cast 0 times. 'cast heal party::' is what the other heal rows of
--    this spec already use.
UPDATE `playerbots_custom_strategy`
   SET `action_line` = 'low mana>cast heal party::Kindle!83'
 WHERE `owner` = 0
   AND `name` = 'pyromancer-flameweaving'
   AND `action_line` = 'low mana>cast heal::Kindle!83';
