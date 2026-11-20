-- CoA 3.1.6: the random bots of a self-found realm that got the starting weapon of their class, once, after being
-- left with no weapon at all (AiPlayerbot.CoaSelfFound.StarterWeapon). bot is the character guid: a bot listed here
-- never gets it again, even after selling or destroying it.
CREATE TABLE IF NOT EXISTS `playerbots_coa_starter_weapon` (
    `bot`  INT UNSIGNED NOT NULL,
    `item` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`bot`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
