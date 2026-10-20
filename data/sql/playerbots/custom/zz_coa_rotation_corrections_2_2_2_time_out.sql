-- CoA Bots: the Time Out! filler row of the two Chronomancer damage rotations (owner 0).
--
-- Rows are chosen by their CONTENT (name + owner 0 + action_line), never by id or idx: the ids differ
-- from one installation to the next. Re-runnable: once applied it changes nothing. Rows with
-- owner <> 0 are never touched. The file name sorts after zz_coa_rotation_corrections_2_1_1_b.sql, so it
-- applies on top of every earlier rotation file (the rows come from the 1.9 file). Rotations are cached per process
-- (CustomStrategy::actionLinesCache): the change shows after a worldserver restart.
--
-- Time Out! (802229, ranks 803896 and 803897) is a Chronomancer class spell from level 14, so every
-- Chronomancer specialization knows it. It puts the caster in a 6 s stasis: it triggers 802228,
-- MOD_STUN and less damage taken on the caster, while it restores mana (15% at once, 5% every 0.5 s).
-- 3 min cooldown. As a 'can cast' filler the bot stunned itself for 6 s in combat whenever the
-- cooldown was over and nothing above the row was castable, short of mana or not.
-- CoA Bots 2.0 already rebuilt chronomancer-time without this filler; it keeps
-- 'low mana>cast::Time Out!!83'. The two damage rotations have no 'low mana' row for Time Out!, so
-- after this file only the 'coa defensive' action (low health) can still cast it.

-- 1. chronomancer-artificer idx 26 'can cast::Time Out!>cast::Time Out!!8'
DELETE FROM `playerbots_custom_strategy`
 WHERE `owner` = 0
   AND `name` = 'chronomancer-artificer'
   AND `action_line` = 'can cast::Time Out!>cast::Time Out!!8';

-- 2. chronomancer-infinite idx 29 'can cast::Time Out!>cast::Time Out!!10'
DELETE FROM `playerbots_custom_strategy`
 WHERE `owner` = 0
   AND `name` = 'chronomancer-infinite'
   AND `action_line` = 'can cast::Time Out!>cast::Time Out!!10';
