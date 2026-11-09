-- CoA Economy: the craft orders placed from the SquidBots Lite addon (AiPlayerbot.CoaEconomy.Artisans.Addon), kept 7 days.
-- state: preparing, sent, awaiting (in the mailbox), paid, returned, cancelled; times are unix seconds.
CREATE TABLE IF NOT EXISTS `coa_economy_orders` (
    `id`       INT UNSIGNED     NOT NULL,
    `player`   INT UNSIGNED     NOT NULL,
    `bot`      INT UNSIGNED     NOT NULL,
    `bot_name` VARCHAR(12)      NOT NULL DEFAULT '',
    `item`     INT UNSIGNED     NOT NULL,
    `qty`      TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `copper`   INT UNSIGNED     NOT NULL DEFAULT 0,
    `state`    VARCHAR(10)      NOT NULL DEFAULT 'preparing',
    `changed`  INT UNSIGNED     NOT NULL DEFAULT 0,
    `eta`      INT UNSIGNED     NOT NULL DEFAULT 0,
    PRIMARY KEY (`id`),
    KEY `player` (`player`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
