-- Proof 3.1.4: puts back the Peacebloom vendor row saved by preuve_illimite_world.sql. World DB, server stopped.
UPDATE npc_vendor v JOIN coa_bench_npc_vendor_save s ON s.entry = v.entry AND s.item = v.item AND s.slot = v.slot
SET v.maxcount = s.maxcount, v.incrtime = s.incrtime;
SELECT v.entry, v.slot, v.item, v.maxcount, v.incrtime FROM npc_vendor v
JOIN coa_bench_npc_vendor_save s ON s.entry = v.entry AND s.item = v.item AND s.slot = v.slot;
DROP TABLE coa_bench_npc_vendor_save;
