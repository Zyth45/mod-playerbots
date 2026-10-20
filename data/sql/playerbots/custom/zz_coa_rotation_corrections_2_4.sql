-- CoA Bots 2.4: dungeon runs without a player (Maraudon, Scholomance, Lower Blackrock Spire, 14-15/10), on top of
-- zz_coa_rotation_corrections_2_3.sql. Default rotations only (owner 0). Rows are chosen by their CONTENT (name + owner 0 +
-- action_line), never by id or idx. Re-runnable: once applied it changes nothing. Rows with owner <> 0 are never
-- touched. Rotations are cached per process (CustomStrategy::actionLinesCache): the change shows after a restart.

DROP PROCEDURE IF EXISTS coa24_add;
DELIMITER $$
CREATE PROCEDURE coa24_add(IN p_name VARCHAR(255), IN p_line TEXT)
BEGIN
  IF EXISTS (SELECT 1 FROM playerbots_custom_strategy WHERE owner = 0 AND name = p_name)
     AND NOT EXISTS (SELECT 1 FROM playerbots_custom_strategy WHERE owner = 0 AND name = p_name AND action_line = p_line) THEN
    INSERT INTO playerbots_custom_strategy (name, idx, owner, action_line)
    SELECT p_name, MAX(idx) + 1, 0, p_line FROM playerbots_custom_strategy WHERE owner = 0 AND name = p_name;
  END IF;
END$$
DELIMITER ;

DROP PROCEDURE IF EXISTS coa24_set;
DELIMITER $$
CREATE PROCEDURE coa24_set(IN p_name VARCHAR(255), IN p_old TEXT, IN p_new TEXT)
BEGIN
  IF NOT EXISTS (SELECT 1 FROM playerbots_custom_strategy WHERE owner = 0 AND name = p_name AND action_line = p_new) THEN
    UPDATE playerbots_custom_strategy SET action_line = p_new WHERE owner = 0 AND name = p_name AND action_line = p_old;
  ELSE
    DELETE FROM playerbots_custom_strategy WHERE owner = 0 AND name = p_name AND action_line = p_old;
  END IF;
END$$
DELIMITER ;

-- 1. Tanks. The 30 yd taunts were on melee rows, so a caster hitting the healer from range was never taunted
--    (Shield Challenge and Writhe: over 1,000 refusals each, "enemy out of melee range"). Unyielding Stand, a 10 yd
--    area taunt, takes the pack instead of waiting for the current target to turn away.
CALL coa24_set('guardian-vanguard', 'lose aggro>cast melee::Shield Challenge!94', 'lose aggro>cast::Shield Challenge!94');
CALL coa24_set('guardian-vanguard', 'lose aggro>cast melee::Unyielding Stand!93', 'medium aoe>cast::Unyielding Stand!93');
CALL coa24_set('reaper-domination', 'lose aggro>cast melee::Writhe!94', 'lose aggro>cast::Writhe!94');

-- 2. Guardian: Reclaim Standards took the standard back one tick after it was placed (about 270 times a run), so
--    Spellwarding never stayed and a fifth of the energy went into standards.
DELETE FROM `playerbots_custom_strategy`
 WHERE `owner` = 0 AND `name` IN ('guardian-vanguard', 'guardian-gladiator')
   AND `action_line` = 'can cast::Reclaim Standards>cast melee::Reclaim Standards!1';

-- 3. Reaper: Reap, free and without cooldown, sat above every spender, so Soul Strike (the self-heal) and Requiem
--    were never cast. Spenders first; without the power they fall through to Reap in the same tick.
CALL coa24_set('reaper-domination', 'medium aoe>cast melee::Reap!87', 'medium aoe>cast melee::Reap!83');
CALL coa24_set('reaper-domination', 'can cast::Reap>cast melee::Reap!82', 'can cast::Reap>cast melee::Reap!78');
CALL coa24_set('reaper-harvest', 'can cast::Soul Strike>cast melee::Soul Strike!8', 'can cast::Soul Strike>cast melee::Soul Strike!78');
CALL coa24_set('reaper-soul', 'can cast::Soul Strike>cast melee::Soul Strike!12', 'can cast::Soul Strike>cast melee::Soul Strike!76');
-- the single-target Rites aim at the enemy; the party Rites stay
DELETE FROM `playerbots_custom_strategy`
 WHERE `owner` = 0 AND `name` IN ('reaper-domination', 'reaper-harvest', 'reaper-soul')
   AND `action_line` LIKE 'can cast::Rite of %>cast melee::Rite of %';

-- 4. Ranger: Survival Potion went at the pull at full health and was on cooldown at 5 of 6 deaths.
CALL coa24_set('ranger-archery', 'can cast::Survival Potion>cast::Survival Potion!21', 'low health>cast::Survival Potion!29');
CALL coa24_set('ranger-brigand', 'can cast::Survival Potion>cast melee::Survival Potion!23', 'low health>cast::Survival Potion!29');
CALL coa24_set('ranger-farstrider', 'can cast::Survival Potion>cast::Survival Potion!83', 'low health>cast::Survival Potion!29');
DELETE FROM `playerbots_custom_strategy`
 WHERE `owner` = 0 AND `name` = 'ranger-farstrider' AND `action_line` = 'medium aoe>cast::Survival Potion!87';
-- stealth-only spells: the Ranger has no stealth in a fight
DELETE FROM `playerbots_custom_strategy`
 WHERE `owner` = 0 AND `name` = 'ranger-archery'
   AND `action_line` IN ('debuff missing::Toxic Dart>cast debuff::Toxic Dart!7', 'can cast::Rusty Shiv>cast::Rusty Shiv!16',
                         'can cast::Sticky Fingers>cast::Sticky Fingers!10', 'can cast::Flank>cast::Flank!8');

-- 5. Sun Cleric: Bless and Daybreak aimed at the enemy (0 casts); Vow of Grace too (Vow of the Eclipse stays);
--    Radiance pulses around the caster, not the tank. Daybreak's instant charges come before a 3 s Illumination.
CALL coa24_set('sun-cleric-blessings', 'party member low health>cast::Bless!85', 'party member low health>cast heal party::Bless!85');
CALL coa24_set('sun-cleric-blessings', 'can cast::Daybreak>cast::Daybreak!29', 'party member critical health>cast heal party::Daybreak!93');
CALL coa24_add('sun-cleric-blessings', 'party member low health>cast heal party::Daybreak!86');
CALL coa24_add('sun-cleric-blessings', 'medium aoe heal>cast::Solar Invocation: Ascension!86');
CALL coa24_set('sun-cleric-blessings', 'can cast::Gavel of Light>cast::Gavel of Light!23', 'can cast::Gavel of Light>cast melee::Gavel of Light!23');
DELETE FROM `playerbots_custom_strategy`
 WHERE `owner` = 0 AND `name` = 'sun-cleric-blessings'
   AND `action_line` IN ('party member low health>cast::Vow of Grace!82', 'medium aoe heal>cast heal tank::Radiance!24',
                         'buff missing::Blazing Chariot>cast buff::Blazing Chariot!30');

-- 6. Starcaller Sentinel: the mana-return shot comes before the bot runs dry, not after.
CALL coa24_set('starcaller-sentinel', 'low mana>cast::Moon Arrow!60', 'medium mana>cast::Moon Arrow!73');

-- 7. Witch Hunter: Infiltrate is refused in any instance and was retried every tick (2,331 times); Stoicism, a
--    3 min defensive, went off with nothing on the bot (coa defensive already uses it).
DELETE FROM `playerbots_custom_strategy`
 WHERE `owner` = 0 AND `name` LIKE 'witch-hunter-%' AND `action_line` LIKE 'can cast::Infiltrate>%::Infiltrate!%';
DELETE FROM `playerbots_custom_strategy`
 WHERE `owner` = 0 AND `name` = 'witch-hunter-houndmaster' AND `action_line` = 'buff missing::Stoicism>cast buff::Stoicism!11';

-- 8. Lines that could never win.
CALL coa24_set('chronomancer-infinite', 'medium aoe>cast::Discordance!60', 'medium aoe>cast::Discordance!85');
DELETE FROM `playerbots_custom_strategy`
 WHERE `owner` = 0 AND `name` = 'chronomancer-infinite'
   AND `action_line` IN ('buff missing::Incarnation of Chaos>cast buff::Incarnation of Chaos!87',
                         'can cast::Wand of Time>cast::Wand of Time!80');
DELETE FROM `playerbots_custom_strategy`
 WHERE `owner` = 0 AND `name` = 'runemaster-riftblade'
   AND `action_line` = 'buff missing::Palm Sigil: Fire>cast buff::Palm Sigil: Fire!77';
DELETE FROM `playerbots_custom_strategy`
 WHERE `owner` = 0 AND `name` = 'templar-crusader' AND `action_line` = 'can cast::Chastise>cast melee::Chastise!9';
DELETE FROM `playerbots_custom_strategy`
 WHERE `owner` = 0 AND `name` = 'barbarian-ancestry'
   AND `action_line` = 'can cast::Whirling Advance>cast melee::Whirling Advance!5';

-- 9. 5 yd strikes on debuff rows, which have no melee check: Break failed 601 times and Condemn 196 times from range.
CALL coa24_set('barbarian-ancestry', 'debuff missing::Break>cast debuff::Break!78', 'debuff missing::Break>cast melee::Break!78');
CALL coa24_set('barbarian-brutality', 'debuff missing::Break>cast debuff::Break!76', 'debuff missing::Break>cast melee::Break!76');
CALL coa24_set('templar-crusader', 'debuff missing::Condemn>cast debuff::Condemn!84', 'debuff missing::Condemn>cast melee::Condemn!84');
CALL coa24_set('templar-crusader', 'debuff missing::Condemn>cast debuff::Condemn!12', 'debuff missing::Condemn>cast melee::Condemn!12');
CALL coa24_set('templar-crusader', 'debuff missing::Blade of Faith>cast debuff::Blade of Faith!29', 'debuff missing::Blade of Faith>cast melee::Blade of Faith!29');
CALL coa24_set('templar-oathkeeper', 'debuff missing::Condemn>cast debuff::Condemn!84', 'debuff missing::Condemn>cast melee::Condemn!84');
CALL coa24_set('templar-oathkeeper', 'debuff missing::Blade of Faith>cast debuff::Blade of Faith!84', 'debuff missing::Blade of Faith>cast melee::Blade of Faith!84');
CALL coa24_set('templar-zealot', 'debuff missing::Condemn>cast debuff::Condemn!86', 'debuff missing::Condemn>cast melee::Condemn!86');

-- 10. Guardian tanks enchant the shield with Spiked Reinforcement (threat) before Jagged; the enchant lasts an hour.
CALL coa24_set('guardian-vanguard', 'can cast::Jagged Reinforcement>cast melee::Jagged Reinforcement!21', 'can cast::Jagged Reinforcement>cast melee::Jagged Reinforcement!3');
CALL coa24_set('guardian-vanguard', 'can cast::Spiked Reinforcement>cast melee::Spiked Reinforcement!4', 'can cast::Spiked Reinforcement>cast melee::Spiked Reinforcement!21');
CALL coa24_set('guardian-gladiator', 'can cast::Jagged Reinforcement>cast melee::Jagged Reinforcement!22', 'can cast::Jagged Reinforcement>cast melee::Jagged Reinforcement!3');
CALL coa24_set('guardian-gladiator', 'can cast::Spiked Reinforcement>cast melee::Spiked Reinforcement!4', 'can cast::Spiked Reinforcement>cast melee::Spiked Reinforcement!22');

-- 11. Player tips from the spec pages (14-15/10).
-- Healers keep their covenant on the tank: Abyssal Covenant (Heretic) and Bless (Sun Cleric) go on the tank when it
-- lacks them; Bless on the hurt ally (5.) stays below.
CALL coa24_add('cultist-heretic', 'can cast::Abyssal Covenant>cast heal tank::Abyssal Covenant!83.5');
CALL coa24_add('sun-cleric-blessings', 'can cast::Bless>cast heal tank::Bless!85.5');
-- Sun Cleric Blessings: no Shoot, Wand, Horusath Blast, Gavel of Wrath, Chains of Light or a lone Solar Invocation:
-- Ascension (it stays on the group heal lines); Gavel of Grace only when out of mana; Dawnfall on big packs;
-- Devotion of Grace when missing; Paragon and Solar Prayer when the healer itself is low.
DELETE FROM `playerbots_custom_strategy`
 WHERE `owner` = 0 AND `name` = 'sun-cleric-blessings'
   AND `action_line` IN ('can cast::Shoot>cast::Shoot!2', 'can cast::Wand>cast::Wand!1',
                         'can cast::Horusath Blast>cast::Horusath Blast!18', 'can cast::Gavel of Wrath>cast::Gavel of Wrath!8',
                         'can cast::Chains of Light>cast::Chains of Light!7',
                         'can cast::Solar Invocation: Ascension>cast::Solar Invocation: Ascension!10');
CALL coa24_set('sun-cleric-blessings', 'can cast::Gavel of Grace>cast melee::Gavel of Grace!28', 'low mana>cast melee::Gavel of Grace!28');
CALL coa24_set('sun-cleric-blessings', 'can cast::Dawnfall>cast::Dawnfall!15', 'high aoe>cast::Dawnfall!15');
CALL coa24_set('sun-cleric-blessings', 'can cast::Devotion of Grace>cast::Devotion of Grace!2', 'buff missing::Devotion of Grace>cast buff::Devotion of Grace!2');
CALL coa24_set('sun-cleric-blessings', 'buff missing::Paragon>cast buff::Paragon!17', 'low health>cast buff::Paragon!30');
CALL coa24_add('sun-cleric-blessings', 'low health>cast heal::Solar Prayer!31');
-- Knight of Xoroth Defiance: Flesh Hook on a single target only; Meatsaw opens on packs; Shieldgore on cooldown;
-- Chains of Malice as a ranged taunt; Black Shield, Sacrificial Circle and Juggernaut kept for low health, one by one.
DELETE FROM `playerbots_custom_strategy`
 WHERE `owner` = 0 AND `name` = 'knight-of-xoroth-defiance' AND `action_line` = 'medium aoe>cast melee::Flesh Hook!88';
CALL coa24_set('knight-of-xoroth-defiance', 'can cast::Meatsaw>cast melee::Meatsaw!4', 'medium aoe>cast melee::Meatsaw!87.5');
CALL coa24_add('knight-of-xoroth-defiance', 'can cast::Shieldgore>cast melee::Shieldgore!81');
CALL coa24_add('knight-of-xoroth-defiance', 'lose aggro>cast::Chains of Malice!93');
CALL coa24_add('knight-of-xoroth-defiance', 'low health>cast buff::Black Shield!91');
CALL coa24_add('knight-of-xoroth-defiance', 'low health>cast::Sacrificial Circle!89');
CALL coa24_set('knight-of-xoroth-defiance', 'buff missing::Juggernaut>cast buff::Juggernaut!30', 'critical health>cast buff::Juggernaut!90');
-- Witch Doctor Shadowhunting: Shadowflare went out again and again on packs; it is put back only when missing.
CALL coa24_set('witch-doctor-shadowhunting', 'light aoe>cast::Shadowflare!80', 'debuff missing::Shadowflare>cast debuff::Shadowflare!80');
DELETE FROM `playerbots_custom_strategy`
 WHERE `owner` = 0 AND `name` = 'witch-doctor-shadowhunting' AND `action_line` = 'can cast::Shadowflare>cast::Shadowflare!11';

-- 12. Venomancer Rot and Vizier: the 2.4 core reads the shapeshift masks of the forms above 32, so Spider Form now
-- forbids their casts and a bot in it only cast Blight Venom (Rot fights at 200-400 a second). They take Weaver Form.
CALL coa24_set('venomancer-rot', 'buff missing::Spider Form>cast buff::Spider Form!95', 'buff missing::Weaver Form>cast buff::Weaver Form!95');
CALL coa24_set('venomancer-vizier', 'buff missing::Spider Form>cast buff::Spider Form!95', 'buff missing::Weaver Form>cast buff::Weaver Form!95');

DROP PROCEDURE IF EXISTS coa24_add;
DROP PROCEDURE IF EXISTS coa24_set;
