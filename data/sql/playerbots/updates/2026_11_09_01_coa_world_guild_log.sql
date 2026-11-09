-- CoA World: the guild orders and gear gifts of each real player, for the SquidBots Lite addon (SQBW GLIST).
-- kind 1 = order (kept 7 days), 2 = gear gift (kept 30 days); purged at start-up. guid = the player's.
CREATE TABLE IF NOT EXISTS `coa_world_guild_log` (
    `id`     INT UNSIGNED NOT NULL,
    `guid`   INT UNSIGNED NOT NULL,
    `kind`   TINYINT UNSIGNED NOT NULL,
    `item`   INT UNSIGNED NOT NULL DEFAULT 0,
    `count`  SMALLINT UNSIGNED NOT NULL DEFAULT 0,
    `state`  VARCHAR(8) NOT NULL DEFAULT '',
    `bot`    VARCHAR(12) NOT NULL DEFAULT '',
    `at`     INT UNSIGNED NOT NULL DEFAULT 0,
    `cod`    INT UNSIGNED NOT NULL DEFAULT 0,
    `reason` VARCHAR(8) NOT NULL DEFAULT '',
    PRIMARY KEY (`id`),
    KEY `guid_at` (`guid`, `at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
