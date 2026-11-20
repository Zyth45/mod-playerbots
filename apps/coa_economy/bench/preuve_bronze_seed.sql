-- Proof 3.1.4, the user's case (16/11): a non-bot account lists 10 Bronze Bar (2841) at 1 silver an item under the
-- cheapest price the house asks for it, in each faction house where the house has some: the house buys it
-- ("house bought material 2841 x10"). Characters DB, server stopped, after the other seeds.
SET @seller := (SELECT c.guid FROM characters c JOIN acore_auth.account u ON u.id = c.account
                WHERE u.username NOT LIKE 'RNDBOT%' AND u.username NOT LIKE 'RNDW%' AND u.username NOT LIKE 'AHBOT%'
                ORDER BY c.account, c.guid LIMIT 1);
DROP TEMPORARY TABLE IF EXISTS seed;
CREATE TEMPORARY TABLE seed (n INT AUTO_INCREMENT PRIMARY KEY, house TINYINT, per_item INT);
INSERT INTO seed (house, per_item)
SELECT a.houseid, MIN(a.buyoutprice DIV i.count) - 100
FROM auctionhouse a JOIN item_instance i ON i.guid = a.itemguid JOIN characters c ON c.guid = a.itemowner
JOIN acore_auth.account u ON u.id = c.account
WHERE u.username LIKE 'AHBOT%' AND i.itemEntry = 2841 AND a.houseid IN (2, 6) AND a.buyoutprice > 0
GROUP BY a.houseid HAVING MIN(a.buyoutprice DIV i.count) > 100;
SET @item0 := (SELECT COALESCE(MAX(guid), 0) FROM item_instance);
SET @auction0 := (SELECT COALESCE(MAX(id), 0) FROM auctionhouse);
INSERT INTO item_instance (guid, itemEntry, owner_guid, count, charges, enchantments, text)
SELECT @item0 + n, 2841, @seller, 10, '0 0 0 0 0 ', '', '' FROM seed WHERE @seller IS NOT NULL;
INSERT INTO auctionhouse (id, houseid, itemguid, itemowner, buyoutprice, time, buyguid, lastbid, startbid, deposit)
SELECT @auction0 + n, house, @item0 + n, @seller, per_item * 10, UNIX_TIMESTAMP() + 172800, 0, 0, per_item * 10, 0
FROM seed WHERE @seller IS NOT NULL;
SELECT @seller AS seller, house, per_item FROM seed;
