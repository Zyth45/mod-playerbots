-- Bench 3.1.4, guild stock by family: random bots' floor materials in the bank (bag 0 slots 39-66 and the bank bags
-- in slots 67-73) and in the bags, by family; bots with some of it in the bank; bots' auctions of floor materials.
-- Characters DB, read only.
SELECT 'bank_by_family' AS what,
       CASE WHEN t.SubClass = 5 THEN 'cloth' WHEN t.SubClass = 7 THEN 'metal_stone' WHEN t.SubClass = 9 THEN 'herb'
            WHEN t.SubClass = 6 THEN 'leather' ELSE 'other' END AS family,
       SUM(inb.inbank) AS stacks_bank, SUM(NOT inb.inbank) AS stacks_bags,
       COUNT(DISTINCT CASE WHEN inb.inbank THEN inb.guid END) AS bots_with_bank
FROM (SELECT inv.guid, i.itemEntry,
             ((inv.bag = 0 AND inv.slot BETWEEN 39 AND 66) OR
              inv.bag IN (SELECT b.item FROM character_inventory b WHERE b.bag = 0 AND b.slot BETWEEN 67 AND 73)) AS inbank
      FROM character_inventory inv JOIN item_instance i ON i.guid = inv.item
      JOIN characters c ON c.guid = inv.guid JOIN acore_auth.account u ON u.id = c.account
      WHERE u.username LIKE 'RNDBOT%') inb
JOIN acore_world.item_template t ON t.entry = inb.itemEntry
WHERE t.class = 7 AND t.SubClass IN (5, 6, 7, 9)
GROUP BY family ORDER BY family;
SELECT 'bots_with_bank_stock' AS what, COUNT(DISTINCT inv.guid) AS bots
FROM character_inventory inv JOIN item_instance i ON i.guid = inv.item
JOIN characters c ON c.guid = inv.guid JOIN acore_auth.account u ON u.id = c.account
JOIN acore_world.item_template t ON t.entry = i.itemEntry
WHERE u.username LIKE 'RNDBOT%' AND t.class = 7 AND t.SubClass IN (5, 6, 7, 9)
  AND ((inv.bag = 0 AND inv.slot BETWEEN 39 AND 66) OR
       inv.bag IN (SELECT b.item FROM character_inventory b WHERE b.bag = 0 AND b.slot BETWEEN 67 AND 73));
SELECT 'bot_auctions_trade_goods' AS what, a.houseid, COUNT(*) AS auctions, COUNT(DISTINCT a.itemowner) AS bots
FROM auctionhouse a JOIN item_instance i ON i.guid = a.itemguid JOIN acore_world.item_template t ON t.entry = i.itemEntry
JOIN characters c ON c.guid = a.itemowner JOIN acore_auth.account u ON u.id = c.account
WHERE u.username LIKE 'RNDBOT%' AND t.class = 7 GROUP BY a.houseid;
