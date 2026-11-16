-- Bench 3.1.4 "farmers": a non-bot character lists common materials in the Alliance (2) and Horde (6) houses, before
-- the server starts. Each material twice per house: at 85 % of the house price measured on the 3.1.4 bench (should
-- sell to the house buyer, up to 90 %) and at 120 % (control: should not sell). Run on the characters DB, server
-- stopped. The seller is the first character of an account that is neither RNDBOT% nor AHBOT%.
SET @seller := (SELECT c.guid FROM characters c JOIN acore_auth.account u ON u.id = c.account
                WHERE u.username NOT LIKE 'RNDBOT%' AND u.username NOT LIKE 'AHBOT%' ORDER BY c.guid LIMIT 1);
SELECT IF(@seller IS NULL, 'NO NON-BOT CHARACTER: create one first', CONCAT('seller ', @seller)) AS seed;

DROP TEMPORARY TABLE IF EXISTS seed;
CREATE TEMPORARY TABLE seed (n INT AUTO_INCREMENT PRIMARY KEY, house TINYINT, item INT, per_item INT, tag VARCHAR(8));
-- house prices per item from banc-hdv-314-B-fin (Alliance, Horde)
INSERT INTO seed (house, item, per_item, tag) VALUES
 (2, 2589,  985 * 85 DIV 100, 'sell'), (2, 2589,  985 * 120 DIV 100, 'control'),
 (6, 2589,  874 * 85 DIV 100, 'sell'), (6, 2589,  874 * 120 DIV 100, 'control'),
 (2, 2318, 2770 * 85 DIV 100, 'sell'), (2, 2318, 2770 * 120 DIV 100, 'control'),
 (6, 2318, 2571 * 85 DIV 100, 'sell'), (6, 2318, 2571 * 120 DIV 100, 'control'),
 (2, 2842, 2707 * 85 DIV 100, 'sell'), (2, 2842, 2707 * 120 DIV 100, 'control'),
 (6, 2842, 2639 * 85 DIV 100, 'sell'), (6, 2842, 2639 * 120 DIV 100, 'control'),
 (2, 3575, 3003 * 85 DIV 100, 'sell'), (2, 3575, 3003 * 120 DIV 100, 'control'),
 (6, 3575, 2520 * 85 DIV 100, 'sell'), (6, 3575, 2520 * 120 DIV 100, 'control'),
 (2, 14047, 421 * 85 DIV 100, 'sell'), (2, 14047, 421 * 120 DIV 100, 'control'),
 (6, 14047, 469 * 85 DIV 100, 'sell'), (6, 14047, 469 * 120 DIV 100, 'control'),
 (2, 2447,  254 * 85 DIV 100, 'sell'), (2, 2447,  254 * 120 DIV 100, 'control'),
 (6, 2447,  256 * 85 DIV 100, 'sell'), (6, 2447,  256 * 120 DIV 100, 'control');

SET @item0 := (SELECT COALESCE(MAX(guid), 0) FROM item_instance);
SET @auction0 := (SELECT COALESCE(MAX(id), 0) FROM auctionhouse);
INSERT INTO item_instance (guid, itemEntry, owner_guid, count, charges, enchantments, text)
SELECT @item0 + n, item, @seller, 20, '0 0 0 0 0 ', '', '' FROM seed WHERE @seller IS NOT NULL;
INSERT INTO auctionhouse (id, houseid, itemguid, itemowner, buyoutprice, time, buyguid, lastbid, startbid, deposit)
SELECT @auction0 + n, house, @item0 + n, @seller, per_item * 20, UNIX_TIMESTAMP() + 172800, 0, 0, per_item * 20, 0
FROM seed WHERE @seller IS NOT NULL;
SELECT tag, COUNT(*) AS auctions, MIN(@auction0 + n) AS first_id, MAX(@auction0 + n) AS last_id FROM seed GROUP BY tag;
