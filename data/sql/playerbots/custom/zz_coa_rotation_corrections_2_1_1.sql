-- CoA Bots 2.1.1: player reports from the specs site, on top of zz_coa_rotation_corrections_2_1.sql
-- ('.' < '_', so this file sorts after it). Default rotations only (owner 0).
--
-- Rows are chosen by their CONTENT (name + owner 0 + action_line), never by id or idx: the ids differ from one
-- installation to the next. Re-runnable: once applied it changes nothing. Rows with owner <> 0 are never touched.
-- Rotations are cached per process (CustomStrategy::actionLinesCache): the change shows after a worldserver restart.

-- 1. Primalist Mountain King: Geode Barrage only on the Aftershock proc (301086: "your next Geode Barrage
--    or Earthquake is instant cast and costs less"). Without it the bot channelled Geode Barrage as a filler, above
--    Wildclaw.
DELETE FROM `playerbots_custom_strategy`
 WHERE `owner` = 0
   AND `name` = 'primalist-mountain-king'
   AND `action_line` = 'can cast::Geode Barrage>cast melee::Geode Barrage!2';

INSERT INTO `playerbots_custom_strategy` (`name`, `idx`, `owner`, `action_line`)
SELECT 'primalist-mountain-king', MAX(`idx`) + 1, 0, 'has aura::Aftershock>cast melee::Geode Barrage!86.5'
  FROM `playerbots_custom_strategy`
 WHERE `owner` = 0 AND `name` = 'primalist-mountain-king'
HAVING COUNT(*) > 0
   AND NOT EXISTS (SELECT 1 FROM `playerbots_custom_strategy`
                    WHERE `owner` = 0 AND `name` = 'primalist-mountain-king'
                      AND `action_line` = 'has aura::Aftershock>cast melee::Geode Barrage!86.5');

-- 2. Primalist Grovekeeper (Yeh and another player): the Aftershock proc makes Geode Barrage instant, so it is spent at once.
--    The 2.1 filler row (channelled when short of Rage, as real Grovekeepers do) stays.
INSERT INTO `playerbots_custom_strategy` (`name`, `idx`, `owner`, `action_line`)
SELECT 'primalist-grovekeeper', MAX(`idx`) + 1, 0, 'has aura::Aftershock>cast::Geode Barrage!86.5'
  FROM `playerbots_custom_strategy`
 WHERE `owner` = 0 AND `name` = 'primalist-grovekeeper'
HAVING COUNT(*) > 0
   AND NOT EXISTS (SELECT 1 FROM `playerbots_custom_strategy`
                    WHERE `owner` = 0 AND `name` = 'primalist-grovekeeper'
                      AND `action_line` = 'has aura::Aftershock>cast::Geode Barrage!86.5');

-- 3. Ranger Archery: Uncover Weakness is spent with 5 stacks of Advantage, right after Serrated Shot
--    (92) has taken its 5 stacks, before Incendiary Shot (91). It sat at 12 as a plain filler.
UPDATE `playerbots_custom_strategy`
   SET `action_line` = 'aura stacks::Advantage,5>cast debuff::Uncover Weakness!91.5'
 WHERE `owner` = 0
   AND `name` = 'ranger-archery'
   AND `action_line` = 'can cast::Uncover Weakness>cast::Uncover Weakness!12';

-- 4. Bloodmage Fleshweaver (two reports): Scarlet Delirium (an area fear) was cast every time it was ready,
--    even with no enemy around, instead of the damage spells the offensive healer's buffs feed on. Fearing packs
--    in a dungeon also scatters them.
DELETE FROM `playerbots_custom_strategy`
 WHERE `owner` = 0
   AND `name` = 'bloodmage-fleshweaver'
   AND `action_line` = 'can cast::Scarlet Delirium>cast::Scarlet Delirium!21';

-- 5. Witch Doctor Voodoo: Spirit Idol (mana for the group, no mana cost) when mana runs low, as Brewing
--    already does, instead of a low filler that was never reached.
UPDATE `playerbots_custom_strategy`
   SET `action_line` = 'low mana>cast::Spirit Idol!85'
 WHERE `owner` = 0
   AND `name` = 'witch-doctor-voodoo'
   AND `action_line` = 'can cast::Spirit Idol>cast::Spirit Idol!8';

-- 6. Starcaller Moon Guard: no Stellar Drift in the tank rotation. It knocks every nearby enemy far away
--    after a few seconds, which pulls more packs and scatters the ones the group is killing.
DELETE FROM `playerbots_custom_strategy`
 WHERE `owner` = 0
   AND `name` = 'starcaller-moon-guard'
   AND `action_line` = 'can cast::Stellar Drift>cast melee::Stellar Drift!11';
