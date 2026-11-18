-- CoA 3.1.6: the real players who muted their bots with the Mute button of the SquidBots addon (all their bots at
-- once, alts and recruited, kept between sessions). player is the character guid. A muted bot still answers orders.
CREATE TABLE IF NOT EXISTS `playerbots_coa_mute` (
    `player` INT UNSIGNED NOT NULL,
    PRIMARY KEY (`player`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
