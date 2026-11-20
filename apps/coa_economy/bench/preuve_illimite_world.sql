-- Proof 3.1.4 (exe final): the vendor guard's unlimited-stock path. Run on a COPY of the world DB, server stopped.
-- One Peacebloom vendor row is set to unlimited stock (maxcount 0); the row is saved first, preuve_illimite_restore.sql
-- puts it back. Expected at start-up (Server.log): "vendor guard loaded N items ... 1 of them in unlimited stock)".
DROP TABLE IF EXISTS coa_bench_npc_vendor_save;
CREATE TABLE coa_bench_npc_vendor_save AS
SELECT * FROM npc_vendor WHERE item = 2447 AND ExtendedCost = 0 ORDER BY entry, slot LIMIT 1;
UPDATE npc_vendor v JOIN coa_bench_npc_vendor_save s ON s.entry = v.entry AND s.item = v.item AND s.slot = v.slot
SET v.maxcount = 0;
SELECT v.entry, v.slot, v.item, v.maxcount, v.incrtime FROM npc_vendor v
JOIN coa_bench_npc_vendor_save s ON s.entry = v.entry AND s.item = v.item AND s.slot = v.slot;
