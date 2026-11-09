-- CoA Economy: the world's last sign of life (AiPlayerbot.CoaEconomy.AuctionClock.*), so that auctions run on game time.
-- name 'last_alive': unix time, written every minute and at shutdown.
CREATE TABLE IF NOT EXISTS `coa_economy_clock` (
    `name`  VARCHAR(32) NOT NULL,
    `value` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
