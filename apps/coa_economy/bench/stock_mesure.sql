-- Guild stock (GuildStock.*): the common materials random bots hold, bags and bank, and who lists them.
-- Run on the characters DB. Read only. Bank: bag 0 slots 39-66 and the bags in slots 67-73.
SELECT 'held' AS what,
       SUM(NOT (inv.bag = 0 AND inv.slot BETWEEN 39 AND 66) AND inv.bag NOT IN (SELECT b.item FROM character_inventory b WHERE b.bag = 0 AND b.slot BETWEEN 67 AND 73)) AS stacks_bags,
       SUM((inv.bag = 0 AND inv.slot BETWEEN 39 AND 66) OR inv.bag IN (SELECT b.item FROM character_inventory b WHERE b.bag = 0 AND b.slot BETWEEN 67 AND 73)) AS stacks_bank,
       SUM(i.count) AS items, COUNT(DISTINCT inv.guid) AS bots
FROM character_inventory inv JOIN item_instance i ON i.guid = inv.item
JOIN characters c ON c.guid = inv.guid JOIN acore_auth.account u ON u.id = c.account
WHERE u.username LIKE 'RNDBOT%' AND i.itemEntry IN (2589,2592,4306,4338,14047,2770,2771,2775,2772,3858,7911,10620,2840,2841,2842,3576,3575,3577,3859,3860,6037,12359,2835,2836,2838,7912,12365,2447,765,2449,785,2450,2452,3820,2453,3369,3355,3356,3357,3818,3821,3358,3819,4625,8831,8836,8838,8839,8845,8846,13464,13463,13465,13466,13467,2318,783,2319,4232,4234,4235,4304,8169,8170,8171,7067,7068,7069,7070,7075,7076,7077,7078,7079,7080,7081,7082,10940,10938,10939,10998,11083,11082,11134,11135,11137,11174,11175,11176,16202,16203,16204);

SELECT 'listed' AS what, a.houseid,
       CASE WHEN u.username LIKE 'AHBOT%' THEN 'house' WHEN u.username LIKE 'RNDBOT%' THEN 'bots' ELSE 'players' END AS seller,
       COUNT(*) AS auctions, SUM(i.count) AS items
FROM auctionhouse a JOIN item_instance i ON i.guid = a.itemguid
LEFT JOIN characters c ON c.guid = a.itemowner LEFT JOIN acore_auth.account u ON u.id = c.account
WHERE i.itemEntry IN (2589,2592,4306,4338,14047,2770,2771,2775,2772,3858,7911,10620,2840,2841,2842,3576,3575,3577,3859,3860,6037,12359,2835,2836,2838,7912,12365,2447,765,2449,785,2450,2452,3820,2453,3369,3355,3356,3357,3818,3821,3358,3819,4625,8831,8836,8838,8839,8845,8846,13464,13463,13465,13466,13467,2318,783,2319,4232,4234,4235,4304,8169,8170,8171,7067,7068,7069,7070,7075,7076,7077,7078,7079,7080,7081,7082,10940,10938,10939,10998,11083,11082,11134,11135,11137,11174,11175,11176,16202,16203,16204)
GROUP BY a.houseid, seller ORDER BY a.houseid, seller;
