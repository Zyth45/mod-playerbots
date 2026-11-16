-- Bench 3.1.4 "farmers": run on the characters DB at the start and end of each arm. Read only.
-- 1. The non-bot seller's seeded auctions still listed (sold = gone), by tag price band.
SET @seller := (SELECT c.guid FROM characters c JOIN acore_auth.account u ON u.id = c.account
                WHERE u.username NOT LIKE 'RNDBOT%' AND u.username NOT LIKE 'AHBOT%' ORDER BY c.guid LIMIT 1);
SELECT 'player_listed' AS what, a.houseid, i.itemEntry, a.buyoutprice DIV i.count AS per_item, COUNT(*) AS auctions
FROM auctionhouse a JOIN item_instance i ON i.guid = a.itemguid WHERE a.itemowner = @seller
GROUP BY a.houseid, i.itemEntry, per_item ORDER BY a.houseid, i.itemEntry, per_item;
-- 2. Gold mailed to the seller by sold auctions (auction mails with money, not yet taken).
SELECT 'player_paid' AS what, COUNT(*) AS mails, SUM(money) AS copper FROM mail WHERE receiver = @seller AND messageType = 2 AND money > 0;
-- 3. Random bots with a profession (any rank), and with a gathering one.
SELECT 'professions' AS what, COUNT(DISTINCT c.guid) AS bots,
       COUNT(DISTINCT CASE WHEN s.skill IN (164,165,171,197,202,333,755,773,182,186,393) THEN c.guid END) AS with_profession,
       COUNT(DISTINCT CASE WHEN s.skill IN (182,186,393) THEN c.guid END) AS with_gathering
FROM characters c JOIN acore_auth.account u ON u.id = c.account LEFT JOIN character_skills s ON s.guid = c.guid
WHERE u.username LIKE 'RNDBOT%';
-- 4. Random bots' gold (copper), all and online.
SELECT 'bot_gold' AS what, COUNT(*) AS bots, SUM(money) AS copper, SUM(online) AS online
FROM characters c JOIN acore_auth.account u ON u.id = c.account WHERE u.username LIKE 'RNDBOT%';
