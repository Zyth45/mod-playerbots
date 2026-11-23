-- CoA 3.2.2: one row per (owner, bot, event) in playerbots_random_bots. Each event was written by a DELETE then an
-- INSERT in a transaction; thousands of bots logging in at once made those transactions deadlock (error 1213) on the
-- table's indexes. The module now writes one INSERT ... ON DUPLICATE KEY UPDATE, which needs this key.
-- The duplicates first: the newest row of each (owner, bot, event) is kept.
DELETE `r1` FROM `playerbots_random_bots` `r1`
JOIN `playerbots_random_bots` `r2`
  ON `r1`.`owner` = `r2`.`owner` AND `r1`.`bot` = `r2`.`bot` AND `r1`.`event` <=> `r2`.`event` AND `r1`.`id` < `r2`.`id`;

-- Added only where it is missing (a realm where this file was applied by hand keeps its key).
SET @coa_key := (SELECT COUNT(*) FROM `information_schema`.`statistics` WHERE `table_schema` = DATABASE()
                 AND `table_name` = 'playerbots_random_bots' AND `index_name` = 'owner_bot_event');
SET @coa_sql := IF(@coa_key = 0,
    'ALTER TABLE `playerbots_random_bots` ADD UNIQUE KEY `owner_bot_event` (`owner`, `bot`, `event`)',
    'SELECT 1');
PREPARE coa_stmt FROM @coa_sql;
EXECUTE coa_stmt;
DEALLOCATE PREPARE coa_stmt;
