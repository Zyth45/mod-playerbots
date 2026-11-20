-- CoA 3.1.6: the trust of each real player in each of his bots (alts or recruited), shown in the Info tab of the
-- SquidBots addon as a rank and a bar. It grows with the monsters killed together, more for a boss. player and bot
-- are character guids. Shown only: no rank opens or locks anything.
CREATE TABLE IF NOT EXISTS `playerbots_coa_trust` (
    `player` INT UNSIGNED NOT NULL,
    `bot`    INT UNSIGNED NOT NULL,
    `score`  INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`player`, `bot`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
