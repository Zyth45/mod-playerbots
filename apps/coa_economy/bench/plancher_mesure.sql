-- Floor of common crafting materials (AHBot.MaterialFloor): offer and prices of the 92 materials, per faction house.
-- Run on the characters DB; @WORLD@ is the world DB name. Read only.
SELECT 'coverage' AS what, h.houseid,
       SUM(COALESCE(x.n, 0) = 0) AS zero, SUM(COALESCE(x.n, 0) = 1) AS one, SUM(COALESCE(x.n, 0) >= 2) AS two_plus,
       COUNT(*) AS materials
FROM (SELECT 2 AS houseid UNION SELECT 6) h
CROSS JOIN @WORLD@.item_template t
LEFT JOIN (SELECT a.houseid, i.itemEntry, COUNT(*) AS n FROM auctionhouse a JOIN item_instance i ON i.guid = a.itemguid
           GROUP BY a.houseid, i.itemEntry) x ON x.houseid = h.houseid AND x.itemEntry = t.entry
WHERE t.entry IN (2589,2592,4306,4338,14047,2770,2771,2775,2772,3858,7911,10620,2840,2841,2842,3576,3575,3577,3859,3860,6037,12359,2835,2836,2838,7912,12365,2447,765,2449,785,2450,2452,3820,2453,3369,3355,3356,3357,3818,3821,3358,3819,4625,8831,8836,8838,8839,8845,8846,13464,13463,13465,13466,13467,2318,783,2319,4232,4234,4235,4304,8169,8170,8171,7067,7068,7069,7070,7075,7076,7077,7078,7079,7080,7081,7082,10940,10938,10939,10998,11083,11082,11134,11135,11137,11174,11175,11176,16202,16203,16204)
GROUP BY h.houseid;

SELECT 'auctions' AS what, a.houseid,
       CASE WHEN u.username LIKE 'AHBOT%' THEN 'house' ELSE 'others' END AS seller,
       COUNT(*) AS auctions, SUM(i.count) AS items, ROUND(SUM(a.buyoutprice) / 10000) AS gold
FROM auctionhouse a JOIN item_instance i ON i.guid = a.itemguid
LEFT JOIN characters c ON c.guid = a.itemowner LEFT JOIN acore_auth.account u ON u.id = c.account
WHERE i.itemEntry IN (2589,2592,4306,4338,14047,2770,2771,2775,2772,3858,7911,10620,2840,2841,2842,3576,3575,3577,3859,3860,6037,12359,2835,2836,2838,7912,12365,2447,765,2449,785,2450,2452,3820,2453,3369,3355,3356,3357,3818,3821,3358,3819,4625,8831,8836,8838,8839,8845,8846,13464,13463,13465,13466,13467,2318,783,2319,4232,4234,4235,4304,8169,8170,8171,7067,7068,7069,7070,7075,7076,7077,7078,7079,7080,7081,7082,10940,10938,10939,10998,11083,11082,11134,11135,11137,11174,11175,11176,16202,16203,16204)
GROUP BY a.houseid, seller;

SELECT 'price' AS what, a.houseid, t.name,
       ROUND(MIN(CASE WHEN u.username LIKE 'AHBOT%' THEN a.buyoutprice / i.count END)) AS house_per_item,
       ROUND(MIN(CASE WHEN u.username NOT LIKE 'AHBOT%' THEN a.buyoutprice / i.count END)) AS others_per_item,
       t.BuyPrice AS vendor_buy
FROM auctionhouse a JOIN item_instance i ON i.guid = a.itemguid JOIN @WORLD@.item_template t ON t.entry = i.itemEntry
LEFT JOIN characters c ON c.guid = a.itemowner LEFT JOIN acore_auth.account u ON u.id = c.account
WHERE i.itemEntry IN (2589, 2842, 3575, 2447, 2318, 7068, 10940, 14047, 12359, 13463)
GROUP BY a.houseid, t.entry ORDER BY a.houseid, t.ItemLevel;

SELECT 'undercut' AS what, COUNT(*) AS house_under_lowest_other
FROM auctionhouse a JOIN item_instance i ON i.guid = a.itemguid
JOIN characters c ON c.guid = a.itemowner JOIN acore_auth.account u ON u.id = c.account AND u.username LIKE 'AHBOT%'
WHERE i.itemEntry IN (2589,2592,4306,4338,14047,2770,2771,2775,2772,3858,7911,10620,2840,2841,2842,3576,3575,3577,3859,3860,6037,12359,2835,2836,2838,7912,12365,2447,765,2449,785,2450,2452,3820,2453,3369,3355,3356,3357,3818,3821,3358,3819,4625,8831,8836,8838,8839,8845,8846,13464,13463,13465,13466,13467,2318,783,2319,4232,4234,4235,4304,8169,8170,8171,7067,7068,7069,7070,7075,7076,7077,7078,7079,7080,7081,7082,10940,10938,10939,10998,11083,11082,11134,11135,11137,11174,11175,11176,16202,16203,16204) AND a.houseid IN (2, 6)
  AND a.buyoutprice / i.count < (SELECT MIN(a2.buyoutprice / i2.count) FROM auctionhouse a2
       JOIN item_instance i2 ON i2.guid = a2.itemguid JOIN characters c2 ON c2.guid = a2.itemowner
       JOIN acore_auth.account u2 ON u2.id = c2.account AND u2.username NOT LIKE 'AHBOT%'
       WHERE a2.houseid = a.houseid AND i2.itemEntry = i.itemEntry AND a2.time < a.time);
