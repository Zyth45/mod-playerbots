-- Bench 3.1.4 "farmers": run on the characters DB at the start and end of each arm. Read only.
-- 1. The non-bot sellers' seeded auctions still listed (sold = gone), by price per item (ferme_joueur_seed.sql).
SELECT 'player_listed' AS what, a.itemowner AS seller, a.houseid, i.itemEntry, a.buyoutprice DIV i.count AS per_item,
       COUNT(*) AS auctions
FROM auctionhouse a JOIN item_instance i ON i.guid = a.itemguid
JOIN characters c ON c.guid = a.itemowner JOIN acore_auth.account u ON u.id = c.account
WHERE u.username NOT LIKE 'RNDBOT%' AND u.username NOT LIKE 'AHBOT%'
GROUP BY a.itemowner, a.houseid, i.itemEntry, per_item ORDER BY a.itemowner, a.houseid, i.itemEntry, per_item;
-- 2. Gold mailed to the non-bot sellers by sold auctions (auction mails with money, not yet taken).
SELECT 'player_paid' AS what, m.receiver AS seller, COUNT(*) AS mails, SUM(m.money) AS copper
FROM mail m JOIN characters c ON c.guid = m.receiver JOIN acore_auth.account u ON u.id = c.account
WHERE u.username NOT LIKE 'RNDBOT%' AND u.username NOT LIKE 'AHBOT%' AND m.messageType = 2 AND m.money > 0
GROUP BY m.receiver;
-- 5. The house characters' mail (items the house won pile up there until the mail expires).
SELECT 'house_mail' AS what, COUNT(DISTINCT m.id) AS mails, COUNT(mi.item_guid) AS items
FROM mail m JOIN characters c ON c.guid = m.receiver JOIN acore_auth.account u ON u.id = c.account
LEFT JOIN mail_items mi ON mi.mail_id = m.id WHERE u.username LIKE 'AHBOT%';
-- 3. Random bots with a profession (any rank), and with a gathering one.
SELECT 'professions' AS what, COUNT(DISTINCT c.guid) AS bots,
       COUNT(DISTINCT CASE WHEN s.skill IN (164,165,171,197,202,333,755,773,182,186,393) THEN c.guid END) AS with_profession,
       COUNT(DISTINCT CASE WHEN s.skill IN (182,186,393) THEN c.guid END) AS with_gathering
FROM characters c JOIN acore_auth.account u ON u.id = c.account LEFT JOIN character_skills s ON s.guid = c.guid
WHERE u.username LIKE 'RNDBOT%';
-- 4. Random bots' gold (copper), all and online.
SELECT 'bot_gold' AS what, COUNT(*) AS bots, SUM(c.money) AS copper, SUM(c.online) AS online
FROM characters c JOIN acore_auth.account u ON u.id = c.account WHERE u.username LIKE 'RNDBOT%';
