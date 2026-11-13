-- CoA Economy: what a realm's auction house holds, for a before/after bench (read only).
-- Run on a realm's characters database; @WORLD@ = its world database. One row per question.
-- House seller = characters of the AHBOT account; bots = accounts named like the realm's bot prefix (RNDBOT...).

SELECT 'auctions' AS what, ah.houseid, COUNT(*) AS n FROM auctionhouse ah GROUP BY ah.houseid;

SELECT 'house seller auctions' AS what, ah.houseid, COUNT(*) AS n
FROM auctionhouse ah JOIN characters c ON c.guid = ah.itemowner JOIN acore_auth.account a ON a.id = c.account
WHERE a.username = 'AHBOT' GROUP BY ah.houseid;

SELECT 'epic gear (weapons, armour)' AS what, ah.houseid, COUNT(*) AS n, COUNT(DISTINCT ii.itemEntry) AS kinds
FROM auctionhouse ah JOIN item_instance ii ON ii.guid = ah.itemguid JOIN @WORLD@.item_template t ON t.entry = ii.itemEntry
WHERE t.Quality = 4 AND t.class IN (2, 4) GROUP BY ah.houseid;

SELECT 'upgrade kits' AS what, ah.houseid, COUNT(*) AS n, MAX(per) AS most_of_one
FROM auctionhouse ah JOIN item_instance ii ON ii.guid = ah.itemguid JOIN @WORLD@.item_template t ON t.entry = ii.itemEntry
JOIN (SELECT ah2.houseid AS h, ii2.itemEntry AS e, COUNT(*) AS per FROM auctionhouse ah2
      JOIN item_instance ii2 ON ii2.guid = ah2.itemguid GROUP BY ah2.houseid, ii2.itemEntry) k
  ON k.h = ah.houseid AND k.e = ii.itemEntry
WHERE t.class = 0 AND t.subclass = 6 GROUP BY ah.houseid;

SELECT 'items listed more than 10 times' AS what, x.houseid, COUNT(*) AS kinds, SUM(x.n) AS auctions, MAX(x.n) AS most
FROM (SELECT ah.houseid, ii.itemEntry, COUNT(*) AS n FROM auctionhouse ah JOIN item_instance ii ON ii.guid = ah.itemguid
      GROUP BY ah.houseid, ii.itemEntry HAVING n > 10) x GROUP BY x.houseid;

SELECT 'top 5 by buyout' AS what, ah.houseid, t.name, ii.count, ah.buyoutprice
FROM auctionhouse ah JOIN item_instance ii ON ii.guid = ah.itemguid JOIN @WORLD@.item_template t ON t.entry = ii.itemEntry
ORDER BY ah.buyoutprice DESC LIMIT 5;
