-- CoA World: "server first" announcements (AiPlayerbot.CoaWorld.ServerFirst.*).
-- One row per first: kind 1 = first to the top level (entry = the level), 2 = first kill of a raid boss
-- (entry = creature entry); per side (team 0 Alliance, 1 Horde). guid 0: taken before the feature was on.
CREATE TABLE IF NOT EXISTS `coa_world_server_first` (
    `kind`  TINYINT UNSIGNED NOT NULL,
    `entry` INT UNSIGNED NOT NULL,
    `team`  TINYINT UNSIGNED NOT NULL,
    `guid`  INT UNSIGNED NOT NULL DEFAULT 0,
    `name`  VARCHAR(12) NOT NULL DEFAULT '',
    `guild` VARCHAR(24) NOT NULL DEFAULT '',
    `at`    INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`kind`, `entry`, `team`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
