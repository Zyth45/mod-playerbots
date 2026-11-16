-- Proof 3.1.4 (exe final), part 2, after preuve_illimite_seed.sql: the gold cap and the limited vendor stocks.
-- Characters DB, server stopped. The house buyer pays at most 90 % of its own lowest ask, that is 0.765 x the
-- item's value (asks run from 0.85 to 1.25 x the value): the prices below sit under it (the farm bench's Thorium at
-- 70 % and Dreamfoil at 85 % of the house's asks were over it, hence never bought nor refused).
--   seller 2: 12 Thorium Bar stacks at 2600 copper an item (5.2 gold a stack): 9 sold, 50 gold a day
--             stops the 10th before the 10 auctions do: "house refused material 12359 ... daily cap (9 auctions").
--   seller 3: Dreamfoil at 2000 copper an item (limited vendor stock, 2 a day: the normal price, sold) and Medium
--             Leather at 1300 (limited stock worth over 10 gold a day: "house refused material 2319 ... vendor guard").
SET @s1acc := (SELECT c.account FROM characters c JOIN acore_auth.account u ON u.id = c.account
               WHERE u.username NOT LIKE 'RNDBOT%' AND u.username NOT LIKE 'RNDW%' AND u.username NOT LIKE 'AHBOT%'
               ORDER BY c.account, c.guid LIMIT 1);
SET @seller2 := (SELECT c.guid FROM characters c JOIN acore_auth.account u ON u.id = c.account
                 WHERE u.username NOT LIKE 'RNDBOT%' AND u.username NOT LIKE 'RNDW%' AND u.username NOT LIKE 'AHBOT%'
                 AND c.account <> @s1acc ORDER BY c.account, c.guid LIMIT 1);
SET @s2acc := (SELECT account FROM characters WHERE guid = @seller2);
SET @seller3 := (SELECT c.guid FROM characters c JOIN acore_auth.account u ON u.id = c.account
                 WHERE u.username NOT LIKE 'RNDBOT%' AND u.username NOT LIKE 'RNDW%' AND u.username NOT LIKE 'AHBOT%'
                 AND c.account <> @s1acc AND c.account <> @s2acc ORDER BY c.account, c.guid LIMIT 1);
SET @ready := @seller2 IS NOT NULL AND @seller3 IS NOT NULL;
SELECT IF(@ready, CONCAT('seller2 ', @seller2, ' seller3 ', @seller3), 'NEED THREE NON-BOT ACCOUNTS') AS seed;
DROP TEMPORARY TABLE IF EXISTS seed;
CREATE TEMPORARY TABLE seed (n INT AUTO_INCREMENT PRIMARY KEY, seller INT, house TINYINT, item INT, per_item INT,
                             tag VARCHAR(8));
INSERT INTO seed (seller, house, item, per_item, tag) VALUES
 (@seller2, 2, 12359, 2600, 'gold'), (@seller2, 2, 12359, 2600, 'gold'), (@seller2, 2, 12359, 2600, 'gold'),
 (@seller2, 2, 12359, 2600, 'gold'), (@seller2, 2, 12359, 2600, 'gold'), (@seller2, 2, 12359, 2600, 'gold'),
 (@seller2, 6, 12359, 2600, 'gold'), (@seller2, 6, 12359, 2600, 'gold'), (@seller2, 6, 12359, 2600, 'gold'),
 (@seller2, 6, 12359, 2600, 'gold'), (@seller2, 6, 12359, 2600, 'gold'), (@seller2, 6, 12359, 2600, 'gold'),
 (@seller3, 2, 13463, 2000, 'dream'), (@seller3, 6, 13463, 2000, 'dream'),
 (@seller3, 2, 2319, 1300, 'medium'), (@seller3, 6, 2319, 1300, 'medium');
SET @item0 := (SELECT COALESCE(MAX(guid), 0) FROM item_instance);
SET @auction0 := (SELECT COALESCE(MAX(id), 0) FROM auctionhouse);
INSERT INTO item_instance (guid, itemEntry, owner_guid, count, charges, enchantments, text)
SELECT @item0 + n, item, seller, 20, '0 0 0 0 0 ', '', '' FROM seed WHERE @ready;
INSERT INTO auctionhouse (id, houseid, itemguid, itemowner, buyoutprice, time, buyguid, lastbid, startbid, deposit)
SELECT @auction0 + n, house, @item0 + n, seller, per_item * 20, UNIX_TIMESTAMP() + 172800, 0, 0, per_item * 20, 0
FROM seed WHERE @ready;
SELECT seller, tag, COUNT(*) AS auctions, SUM(per_item * 20) AS copper FROM seed GROUP BY seller, tag;
