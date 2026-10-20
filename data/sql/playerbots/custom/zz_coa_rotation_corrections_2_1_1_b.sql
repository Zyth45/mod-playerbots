-- CoA Bots 2.1.1, second batch of the specs site reports, on top of zz_coa_rotation_corrections_2_1_1.sql
-- ('.' < '_', so this file sorts after it). Default rotations only (owner 0).
--
-- Rows are chosen by their CONTENT (name + owner 0 + action_line), never by id or idx: the ids differ from one
-- installation to the next. Re-runnable: once applied it changes nothing. Rows with owner <> 0 are never touched.
-- Rotations are cached per process (CustomStrategy::actionLinesCache): the change shows after a worldserver restart.
--
-- 1. Witch Doctor wards and idols, all three specs (a player's report on Voodoo: "near low mana the bots spam a cheap ward and stay too low for anything else,
-- and never use Spirit Idol"): every ward and idol sat in the rotation as 'can cast'. None of them has a cooldown
-- except Spirit Idol, Spirit Link Idol, Swift Idol, Mimic Ward and Stasis Ward, and they cost 5 to 25% of base
-- mana. Once the damage spells were out of reach the bot cycled through them, and since only one Ward and one Idol
-- can stand at a time ("Can only have 1 Ward/Idol active at a time"), each cast replaced the previous one, Spirit
-- Idol included.
--   - The niche idols (Serene: spell pushback, Cleansing: poison and disease, Dark and Jungle: resistances, Swift:
--     speed) and Graven Effigy (crowd control duration) leave the rotations.
--   - Damage specs keep Serpent Ward (creature 50105) and drop Healing Ward, which takes the same Ward slot; the
--     healer (Brewing) keeps Healing Ward (creature 50104). Both are dropped only when theirs is not standing.
--   - Spirit Idol only on low mana, as in Brewing (Voodoo: zz_coa_rotation_corrections_2_1_1.sql).

DELETE FROM `playerbots_custom_strategy`
 WHERE `owner` = 0
   AND `name` IN ('witch-doctor-voodoo', 'witch-doctor-shadowhunting', 'witch-doctor-brewing')
   AND `action_line` IN ('can cast::Serene Idol>cast::Serene Idol!29',
                         'can cast::Cleansing Idol>cast::Cleansing Idol!28',
                         'can cast::Dark Idol>cast::Dark Idol!20',
                         'can cast::Dark Idol>cast::Dark Idol!22',
                         'can cast::Dark Idol>cast::Dark Idol!23',
                         'can cast::Jungle Idol>cast::Jungle Idol!18',
                         'can cast::Jungle Idol>cast::Jungle Idol!20',
                         'can cast::Jungle Idol>cast::Jungle Idol!21',
                         'can cast::Swift Idol>cast::Swift Idol!22',
                         'can cast::Graven Effigy>cast::Graven Effigy!16',
                         'can cast::Graven Effigy>cast::Graven Effigy!18',
                         'can cast::Graven Effigy>cast::Graven Effigy!19');

DELETE FROM `playerbots_custom_strategy`
 WHERE `owner` = 0
   AND `name` IN ('witch-doctor-voodoo', 'witch-doctor-shadowhunting')
   AND `action_line` = 'can cast::Healing Ward>cast::Healing Ward!30';

UPDATE `playerbots_custom_strategy`
   SET `action_line` = 'coa summon missing::50105>cast::Serpent Ward!1'
 WHERE `owner` = 0
   AND `name` IN ('witch-doctor-voodoo', 'witch-doctor-shadowhunting')
   AND `action_line` = 'can cast::Serpent Ward>cast::Serpent Ward!1';

UPDATE `playerbots_custom_strategy`
   SET `action_line` = 'coa summon missing::50104>cast::Healing Ward!30'
 WHERE `owner` = 0
   AND `name` = 'witch-doctor-brewing'
   AND `action_line` = 'can cast::Healing Ward>cast::Healing Ward!30';

UPDATE `playerbots_custom_strategy`
   SET `action_line` = 'low mana>cast::Spirit Idol!85'
 WHERE `owner` = 0
   AND `name` = 'witch-doctor-shadowhunting'
   AND `action_line` = 'can cast::Spirit Idol>cast::Spirit Idol!9';

DELETE FROM `playerbots_custom_strategy`
 WHERE `owner` = 0
   AND `name` = 'witch-doctor-brewing'
   AND `action_line` = 'can cast::Spirit Idol>cast::Spirit Idol!6';

-- Adds a default row when it is missing, at the end of its rotation (the priority is in the line, not in idx).
DROP PROCEDURE IF EXISTS coa211_add;
DELIMITER $$
CREATE PROCEDURE coa211_add(IN p_name VARCHAR(255), IN p_line TEXT)
BEGIN
  IF EXISTS (SELECT 1 FROM playerbots_custom_strategy WHERE owner = 0 AND name = p_name)
     AND NOT EXISTS (SELECT 1 FROM playerbots_custom_strategy WHERE owner = 0 AND name = p_name AND action_line = p_line) THEN
    INSERT INTO playerbots_custom_strategy (name, idx, owner, action_line)
    SELECT p_name, MAX(idx) + 1, 0, p_line FROM playerbots_custom_strategy WHERE owner = 0 AND name = p_name;
  END IF;
END$$
DELIMITER ;

-- 2. Starcaller Moon Guard, tank (a player's report): it opened pulls with a bow shot the mob never let it finish
--    and lost threat on packs. It now grabs packs in melee with Starsweep and Starburst, answers a lost target
--    with Warden's Blade then Lunar Lance after the taunt, and spends Celestial Strike (it restores mana) first
--    under 40% mana, where it used to sit at 10 to 20% in long fights. Huntress Shot (a bow shot) goes.
DELETE FROM `playerbots_custom_strategy`
 WHERE `owner` = 0
   AND `name` = 'starcaller-moon-guard'
   AND `action_line` = 'can cast::Huntress Shot>cast melee::Huntress Shot!8';

UPDATE `playerbots_custom_strategy`
   SET `action_line` = 'medium aoe>cast melee::Starsweep!86.5'
 WHERE `owner` = 0
   AND `name` = 'starcaller-moon-guard'
   AND `action_line` = 'can cast::Starsweep>cast melee::Starsweep!25';

CALL coa211_add('starcaller-moon-guard', 'lose aggro>cast melee::Warden''s Blade!93');
CALL coa211_add('starcaller-moon-guard', 'lose aggro>cast debuff::Lunar Lance!92');
CALL coa211_add('starcaller-moon-guard', 'medium mana>cast melee::Celestial Strike!88');

-- 3. Cultist Heretic, healer (a player's report): Tentacle of C'Thun is placed often while healing: its damage
--    ticks feed the heals, and its mana cost is removed above 60 Insanity. 2.0 took both tentacles out because they
--    replaced each other ("You may only have 1 Old God Tentacle"); only this one comes back, and only while it is
--    not standing (creature 50272). Forbidden Ritual, a shield on the whole group, answers group damage.
CALL coa211_add('cultist-heretic', 'coa summon missing::50272>cast::Tentacle of C''Thun!29');
CALL coa211_add('cultist-heretic', 'medium aoe heal>cast::Forbidden Ritual!89');

-- 4. Necromancer Animation (a player who ran it with their guild): Harvest Plague, then the three animations
--    (Skeletal Archer, Tomb King, Plaguefather), Graveyard when it is up, Unholy Frenzy, Command: Undead while
--    there is Runic Power, Death's Due when up, Blight left to run out (its final burst matters) and Lichfrost on
--    a blighted target; Glacial Tap and Crypt Swarm only refill Runic Power. Never March of the Dead on a single
--    target (long cast): it stays for packs. The summons stay the six Greater Skeletal Warriors: the other raises
--    and animations (Abomination, Ghoul, Decaying Colossus, Skeletal Rogue, Bone Wraith, Skeletal Smith) go.
DELETE FROM `playerbots_custom_strategy`
 WHERE `owner` = 0
   AND `name` = 'necromancer-animation'
   AND `action_line` IN ('can cast::March of the Dead>cast::March of the Dead!81',
                         'can cast::Raise: Abomination>cast::Raise: Abomination!30',
                         'can cast::Raise: Ghoul>cast::Raise: Ghoul!28',
                         'can cast::Raise: Decaying Colossus>cast::Raise: Decaying Colossus!27',
                         'can cast::Animate: Bone Wraith>cast::Animate: Bone Wraith!21',
                         'can cast::Animate: Skeletal Smith>cast::Animate: Skeletal Smith!14',
                         'can cast::Raise: Skeletal Rogue>cast::Raise: Skeletal Rogue!10');

UPDATE `playerbots_custom_strategy` SET `action_line` = 'debuff missing::Harvest Plague>cast debuff::Harvest Plague!93'
 WHERE `owner` = 0 AND `name` = 'necromancer-animation'
   AND `action_line` = 'debuff missing::Harvest Plague>cast debuff::Harvest Plague!86';
UPDATE `playerbots_custom_strategy` SET `action_line` = 'can cast::Graveyard>cast::Graveyard!88.5'
 WHERE `owner` = 0 AND `name` = 'necromancer-animation'
   AND `action_line` = 'can cast::Graveyard>cast::Graveyard!19';
UPDATE `playerbots_custom_strategy` SET `action_line` = 'coa resource::runic power,30>cast::Command: Undead!86.5'
 WHERE `owner` = 0 AND `name` = 'necromancer-animation'
   AND `action_line` = 'coa resource::runic power,30>cast::Command: Undead!84';
UPDATE `playerbots_custom_strategy` SET `action_line` = 'target has aura::Blight>cast::Lichfrost!83.5'
 WHERE `owner` = 0 AND `name` = 'necromancer-animation'
   AND `action_line` = 'can cast::Lichfrost>cast::Lichfrost!79';
UPDATE `playerbots_custom_strategy` SET `action_line` = 'can cast::Glacial Tap>cast::Glacial Tap!80.5'
 WHERE `owner` = 0 AND `name` = 'necromancer-animation'
   AND `action_line` = 'can cast::Glacial Tap>cast::Glacial Tap!84';

CALL coa211_add('necromancer-animation', 'debuff missing::Death''s Due>cast debuff::Death''s Due!86');
CALL coa211_add('necromancer-animation', 'buff missing::Bone Ward>cast buff::Bone Ward!23');

DROP PROCEDURE IF EXISTS coa211_add;
