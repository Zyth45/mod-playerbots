-- Bench 3.1.4, guild stock: random bots start with materials of their level and gathering skills, 3 full stacks of
-- each (the guild stock keeps 2 stacks, GuildStock.Stacks): the deposit to the bank and the surplus sale both have
-- something to do from the first stop at a vendor or an auctioneer. Characters DB (MySQL 8), server stopped.
-- Every random bot (RNDBOT%) of level 10 or more: its level's cloth; with Mining 186 its level's ore; with Herbalism
-- 182 its level's herb; with Skinning 393 its level's leather. Only into free backpack slots (bag 0, slots 23-38),
-- in that order: a bot with fewer free slots gets fewer stacks.
DROP TEMPORARY TABLE IF EXISTS pre_want;
CREATE TEMPORARY TABLE pre_want (guid INT UNSIGNED, item INT UNSIGNED, k INT);
INSERT INTO pre_want (guid, item, k)
SELECT c.guid,
       CASE WHEN c.level < 15 THEN 2589 WHEN c.level < 25 THEN 2592 WHEN c.level < 35 THEN 4306
            WHEN c.level < 45 THEN 4338 ELSE 14047 END, n.k
FROM characters c JOIN acore_auth.account u ON u.id = c.account
JOIN (SELECT 1 AS k UNION ALL SELECT 2 UNION ALL SELECT 3) n
WHERE u.username LIKE 'RNDBOT%' AND c.level >= 10;
INSERT INTO pre_want (guid, item, k)
SELECT c.guid,
       CASE WHEN c.level < 15 THEN 2770 WHEN c.level < 25 THEN 2771 WHEN c.level < 35 THEN 2772
            WHEN c.level < 45 THEN 3858 ELSE 10620 END, 10 + n.k
FROM characters c JOIN acore_auth.account u ON u.id = c.account
JOIN character_skills s ON s.guid = c.guid AND s.skill = 186
JOIN (SELECT 1 AS k UNION ALL SELECT 2 UNION ALL SELECT 3) n
WHERE u.username LIKE 'RNDBOT%' AND c.level >= 10;
INSERT INTO pre_want (guid, item, k)
SELECT c.guid,
       CASE WHEN c.level < 15 THEN 2447 WHEN c.level < 25 THEN 2453 WHEN c.level < 35 THEN 3356
            WHEN c.level < 45 THEN 3818 ELSE 13464 END, 20 + n.k
FROM characters c JOIN acore_auth.account u ON u.id = c.account
JOIN character_skills s ON s.guid = c.guid AND s.skill = 182
JOIN (SELECT 1 AS k UNION ALL SELECT 2 UNION ALL SELECT 3) n
WHERE u.username LIKE 'RNDBOT%' AND c.level >= 10;
INSERT INTO pre_want (guid, item, k)
SELECT c.guid,
       CASE WHEN c.level < 20 THEN 2318 WHEN c.level < 30 THEN 2319 WHEN c.level < 40 THEN 4234
            WHEN c.level < 50 THEN 4304 ELSE 8170 END, 30 + n.k
FROM characters c JOIN acore_auth.account u ON u.id = c.account
JOIN character_skills s ON s.guid = c.guid AND s.skill = 393
JOIN (SELECT 1 AS k UNION ALL SELECT 2 UNION ALL SELECT 3) n
WHERE u.username LIKE 'RNDBOT%' AND c.level >= 10;

-- Free backpack slots, numbered per bot; wanted stacks numbered per bot; matched rank to rank.
DROP TEMPORARY TABLE IF EXISTS pre_slots;
CREATE TEMPORARY TABLE pre_slots AS
SELECT w.guid, s.slot, ROW_NUMBER() OVER (PARTITION BY w.guid ORDER BY s.slot) AS r
FROM (SELECT DISTINCT guid FROM pre_want) w
JOIN (SELECT 23 AS slot UNION ALL SELECT 24 UNION ALL SELECT 25 UNION ALL SELECT 26 UNION ALL SELECT 27
      UNION ALL SELECT 28 UNION ALL SELECT 29 UNION ALL SELECT 30 UNION ALL SELECT 31 UNION ALL SELECT 32
      UNION ALL SELECT 33 UNION ALL SELECT 34 UNION ALL SELECT 35 UNION ALL SELECT 36 UNION ALL SELECT 37
      UNION ALL SELECT 38) s
LEFT JOIN character_inventory ci ON ci.guid = w.guid AND ci.bag = 0 AND ci.slot = s.slot
WHERE ci.item IS NULL;
DROP TEMPORARY TABLE IF EXISTS pre_put;
CREATE TEMPORARY TABLE pre_put AS
SELECT x.guid, x.item, p.slot, ROW_NUMBER() OVER (ORDER BY x.guid, x.k) AS n
FROM (SELECT guid, item, k, ROW_NUMBER() OVER (PARTITION BY guid ORDER BY k) AS r FROM pre_want) x
JOIN pre_slots p ON p.guid = x.guid AND p.r = x.r;

SET @item0 := (SELECT COALESCE(MAX(guid), 0) FROM item_instance);
INSERT INTO item_instance (guid, itemEntry, owner_guid, count, charges, enchantments, text)
SELECT @item0 + n, item, guid, 20, '0 0 0 0 0 ', '', '' FROM pre_put;
INSERT INTO character_inventory (guid, bag, slot, item)
SELECT guid, 0, slot, @item0 + n FROM pre_put;
SELECT COUNT(*) AS stacks, COUNT(DISTINCT guid) AS bots,
       SUM(item IN (2589, 2592, 4306, 4338, 14047)) AS cloth, SUM(item IN (2770, 2771, 2772, 3858, 10620)) AS ore,
       SUM(item IN (2447, 2453, 3356, 3818, 13464)) AS herb, SUM(item IN (2318, 2319, 4234, 4304, 8170)) AS leather
FROM pre_put;
