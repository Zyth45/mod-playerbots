-- Proof 3.1.4 (exe final), part 1: a non-bot account lists Peacebloom (vendor price 40, guard 30 copper an item once its stock is
-- unlimited) in the Alliance (2) and Horde (6) houses. Characters DB, server stopped.
--   'over'   150 and 120 copper an item: the house would pay about 180 without the guard (90 % of its lowest ask,
--            0.85 x its value); the guard stops it:
--            "house refused material 2447 x20 ... vendor guard (pays 30 copper an item at most, vendor price 40)".
--   'under'  28 copper an item: bought, "house bought material 2447 x20".
SET @seller := (SELECT c.guid FROM characters c JOIN acore_auth.account u ON u.id = c.account
                WHERE u.username NOT LIKE 'RNDBOT%' AND u.username NOT LIKE 'RNDW%' AND u.username NOT LIKE 'AHBOT%'
                ORDER BY c.account, c.guid LIMIT 1);
SELECT IF(@seller IS NULL, 'NEED A NON-BOT ACCOUNT WITH A CHARACTER', CONCAT('seller ', @seller)) AS seed;
DROP TEMPORARY TABLE IF EXISTS seed;
CREATE TEMPORARY TABLE seed (n INT AUTO_INCREMENT PRIMARY KEY, house TINYINT, per_item INT, tag VARCHAR(8));
INSERT INTO seed (house, per_item, tag) VALUES
 (2, 150, 'over'), (6, 150, 'over'), (2, 120, 'over'), (6, 120, 'over'), (2, 28, 'under'), (6, 28, 'under');
SET @item0 := (SELECT COALESCE(MAX(guid), 0) FROM item_instance);
SET @auction0 := (SELECT COALESCE(MAX(id), 0) FROM auctionhouse);
INSERT INTO item_instance (guid, itemEntry, owner_guid, count, charges, enchantments, text)
SELECT @item0 + n, 2447, @seller, 20, '0 0 0 0 0 ', '', '' FROM seed WHERE @seller IS NOT NULL;
INSERT INTO auctionhouse (id, houseid, itemguid, itemowner, buyoutprice, time, buyguid, lastbid, startbid, deposit)
SELECT @auction0 + n, house, @item0 + n, @seller, per_item * 20, UNIX_TIMESTAMP() + 172800, 0, 0, per_item * 20, 0
FROM seed WHERE @seller IS NOT NULL;
SELECT tag, COUNT(*) AS auctions FROM seed GROUP BY tag;
