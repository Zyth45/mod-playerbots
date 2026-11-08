-- CoA Economy: market prices learned from real auction sales (AiPlayerbot.CoaEconomy.Market.*).
-- One row per item: usual price per item and the low/high band, in copper; sales counted; last sale (game time).
CREATE TABLE IF NOT EXISTS `coa_economy_market` (
    `item`    INT UNSIGNED NOT NULL,
    `usual`   BIGINT UNSIGNED NOT NULL DEFAULT 0,
    `low`     BIGINT UNSIGNED NOT NULL DEFAULT 0,
    `high`    BIGINT UNSIGNED NOT NULL DEFAULT 0,
    `sales`   INT UNSIGNED NOT NULL DEFAULT 0,
    `updated` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`item`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
