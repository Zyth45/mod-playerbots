-- Bench 3.1.4 "farmers": two non-bot accounts list common materials in the Alliance (2) and Horde (6) houses before
-- the server starts. Run on the characters DB, server stopped. Prices per item from the house prices measured on the
-- 3.1.4 bench (banc-hdv-314-B-fin).
-- Seller 1 (first character of the first account that is neither RNDBOT% nor AHBOT%), the count cap and the guards:
--   'sell'     13 auctions at 70 % of the house price that should sell (Linen too: a limited vendor stock is paid the
--              normal price); with 10 a day per account, 10 sold and 3 refused ("house refused material ... daily
--              cap" in the log).
--   'vendor'   Peacebloom at 70 % of the house price: sold by NPC vendors in unlimited stock, over the vendor guard
--              (75 % of the vendor price), never bought.
--   'pb200'    Peacebloom at 200 copper (vendor 40, guard 30): never bought.
--   'pb28'     Peacebloom at 28 copper: under the guard, bought (counts in the 10).
--   'control'  120 % of the house price: never bought.
-- Seller 2 (first character of another such account), the gold cap: 10 Thorium Bar stacks at 70 % (about 6 gold
-- each); 50 gold a day lets about 8 sell, the next ones are refused ("daily cap" in the log).
-- Seller 3 (a third such account), the limited vendor stocks: Dreamfoil at 85 % of the house price (2 a day at its
-- vendor: the normal price, sold) and Medium Leather at 70 % (288 a day at its vendor, over 10 gold a day: the guard
-- holds, never bought).
SET @seller1 := (SELECT c.guid FROM characters c JOIN acore_auth.account u ON u.id = c.account
                 WHERE u.username NOT LIKE 'RNDBOT%' AND u.username NOT LIKE 'AHBOT%' ORDER BY c.account, c.guid LIMIT 1);
SET @account1 := (SELECT account FROM characters WHERE guid = @seller1);
SET @seller2 := (SELECT c.guid FROM characters c JOIN acore_auth.account u ON u.id = c.account
                 WHERE u.username NOT LIKE 'RNDBOT%' AND u.username NOT LIKE 'AHBOT%' AND c.account <> @account1
                 ORDER BY c.account, c.guid LIMIT 1);
SET @account2 := (SELECT account FROM characters WHERE guid = @seller2);
SET @seller3 := (SELECT c.guid FROM characters c JOIN acore_auth.account u ON u.id = c.account
                 WHERE u.username NOT LIKE 'RNDBOT%' AND u.username NOT LIKE 'AHBOT%' AND c.account <> @account1
                 AND c.account <> @account2 ORDER BY c.account, c.guid LIMIT 1);
SET @ready := @seller1 IS NOT NULL AND @seller2 IS NOT NULL AND @seller3 IS NOT NULL;
SELECT IF(@ready, CONCAT('seller1 ', @seller1, ' seller2 ', @seller2, ' seller3 ', @seller3),
          'NEED THREE NON-BOT ACCOUNTS WITH A CHARACTER: create them first') AS seed;

DROP TEMPORARY TABLE IF EXISTS seed;
CREATE TEMPORARY TABLE seed (n INT AUTO_INCREMENT PRIMARY KEY, seller INT, house TINYINT, item INT, per_item INT,
                             tag VARCHAR(8));
INSERT INTO seed (seller, house, item, per_item, tag) VALUES
 -- seller 1, should sell (13): Light Leather x3, Silver Bar x2, Iron Bar x2, Runecloth x2, Linen x2, Peacebloom at 28 x2
 (@seller1, 2, 2318, 2770 * 70 DIV 100, 'sell'), (@seller1, 2, 2318, 2770 * 70 DIV 100, 'sell'),
 (@seller1, 6, 2318, 2571 * 70 DIV 100, 'sell'),
 (@seller1, 2, 2842, 2707 * 70 DIV 100, 'sell'), (@seller1, 6, 2842, 2639 * 70 DIV 100, 'sell'),
 (@seller1, 2, 2589, 985 * 70 DIV 100, 'sell'), (@seller1, 6, 2589, 874 * 70 DIV 100, 'sell'),
 (@seller1, 2, 3575, 3003 * 70 DIV 100, 'sell'), (@seller1, 6, 3575, 2520 * 70 DIV 100, 'sell'),
 (@seller1, 2, 14047, 421 * 70 DIV 100, 'sell'), (@seller1, 6, 14047, 469 * 70 DIV 100, 'sell'),
 (@seller1, 2, 2447, 28, 'pb28'), (@seller1, 6, 2447, 28, 'pb28'),
 -- seller 1, an unlimited vendor good over the guard (never)
 (@seller1, 2, 2447, 254 * 70 DIV 100, 'vendor'), (@seller1, 6, 2447, 256 * 70 DIV 100, 'vendor'),
 (@seller1, 2, 2447, 200, 'pb200'), (@seller1, 6, 2447, 200, 'pb200'),
 -- seller 1, controls at 120 % (never)
 (@seller1, 2, 2589, 985 * 120 DIV 100, 'control'), (@seller1, 6, 2589, 874 * 120 DIV 100, 'control'),
 (@seller1, 2, 2318, 2770 * 120 DIV 100, 'control'), (@seller1, 6, 2318, 2571 * 120 DIV 100, 'control'),
 (@seller1, 2, 2842, 2707 * 120 DIV 100, 'control'), (@seller1, 6, 2842, 2639 * 120 DIV 100, 'control'),
 (@seller1, 2, 3575, 3003 * 120 DIV 100, 'control'), (@seller1, 6, 3575, 2520 * 120 DIV 100, 'control'),
 (@seller1, 2, 14047, 421 * 120 DIV 100, 'control'), (@seller1, 6, 14047, 469 * 120 DIV 100, 'control'),
 (@seller1, 2, 2447, 254 * 120 DIV 100, 'control'), (@seller1, 6, 2447, 256 * 120 DIV 100, 'control'),
 -- seller 2, gold cap: Thorium Bar x10 at 70 %
 (@seller2, 2, 12359, 4238 * 70 DIV 100, 'gold'), (@seller2, 2, 12359, 4238 * 70 DIV 100, 'gold'),
 (@seller2, 2, 12359, 4238 * 70 DIV 100, 'gold'), (@seller2, 2, 12359, 4238 * 70 DIV 100, 'gold'),
 (@seller2, 2, 12359, 4238 * 70 DIV 100, 'gold'),
 (@seller2, 6, 12359, 4517 * 70 DIV 100, 'gold'), (@seller2, 6, 12359, 4517 * 70 DIV 100, 'gold'),
 (@seller2, 6, 12359, 4517 * 70 DIV 100, 'gold'), (@seller2, 6, 12359, 4517 * 70 DIV 100, 'gold'),
 (@seller2, 6, 12359, 4517 * 70 DIV 100, 'gold'),
 -- seller 3, limited vendor stocks: Dreamfoil sells, Medium Leather keeps the guard
 (@seller3, 2, 13463, 5877 * 85 DIV 100, 'dream'), (@seller3, 6, 13463, 5015 * 85 DIV 100, 'dream'),
 (@seller3, 2, 2319, 2596 * 70 DIV 100, 'medium'), (@seller3, 6, 2319, 2596 * 70 DIV 100, 'medium');

SET @item0 := (SELECT COALESCE(MAX(guid), 0) FROM item_instance);
SET @auction0 := (SELECT COALESCE(MAX(id), 0) FROM auctionhouse);
INSERT INTO item_instance (guid, itemEntry, owner_guid, count, charges, enchantments, text)
SELECT @item0 + n, item, seller, 20, '0 0 0 0 0 ', '', '' FROM seed WHERE @ready;
INSERT INTO auctionhouse (id, houseid, itemguid, itemowner, buyoutprice, time, buyguid, lastbid, startbid, deposit)
SELECT @auction0 + n, house, @item0 + n, seller, per_item * 20, UNIX_TIMESTAMP() + 172800, 0, 0, per_item * 20, 0
FROM seed WHERE @ready;
SELECT seller, tag, COUNT(*) AS auctions, SUM(per_item * 20) AS copper FROM seed GROUP BY seller, tag ORDER BY seller, tag;
