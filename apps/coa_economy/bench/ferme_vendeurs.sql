-- Bench 3.1.4: the vendor guard's table (CoaAuctionHouseBot::LoadVendorBuyPrices) as the running world DB has it,
-- restricted to the 92 floor materials, to compare with the 28 found in CoA's SQL files. Run on the world DB.
SELECT t.entry, t.name, MIN(t.BuyPrice) AS vendor_price, MIN(v.maxcount) AS min_maxcount, COUNT(*) AS vendor_rows
FROM npc_vendor v JOIN item_template t ON t.entry = v.item
WHERE v.ExtendedCost = 0 AND t.BuyPrice > 0 AND t.entry IN (2589,2592,4306,4338,14047,2770,2771,2775,2772,3858,7911,10620,2840,2841,2842,3576,3575,3577,3859,3860,6037,12359,2835,2836,2838,7912,12365,2447,765,2449,785,2450,2452,3820,2453,3369,3355,3356,3357,3818,3821,3358,3819,4625,8831,8836,8838,8839,8845,8846,13464,13463,13465,13466,13467,2318,783,2319,4232,4234,4235,4304,8169,8170,8171,7067,7068,7069,7070,7075,7076,7077,7078,7079,7080,7081,7082,10940,10938,10939,10998,11083,11082,11134,11135,11137,11174,11175,11176,16202,16203,16204)
GROUP BY t.entry, t.name ORDER BY t.entry;
SELECT COUNT(DISTINCT t.entry) AS all_vendor_items FROM npc_vendor v JOIN item_template t ON t.entry = v.item
WHERE v.ExtendedCost = 0 AND t.BuyPrice > 0;
