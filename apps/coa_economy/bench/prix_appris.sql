-- CoA Economy: proof of the update step marketScale (read only). Run on a realm's playerbots database, before and after
-- the update, and after a second run: the learned prices times PriceScalePercent / 100 once, then unchanged.
SELECT 'learned prices' AS what, COUNT(*) AS items, SUM(usual) AS usual, SUM(low) AS low, SUM(high) AS high
FROM coa_economy_market;
SELECT 'mark' AS what, name, done_at, detail FROM coa_update_marks WHERE name = 'marketScale';
SELECT 'sample' AS what, item, usual, low, high FROM coa_economy_market ORDER BY sales DESC, item LIMIT 5;
