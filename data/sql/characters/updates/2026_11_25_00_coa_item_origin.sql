-- CoA World 3.2.3: where each worn piece came from (dashboard armory). The module also creates it at start-up.
CREATE TABLE IF NOT EXISTS `coa_item_origin` (
  `item`   INT UNSIGNED NOT NULL,
  `owner`  INT UNSIGNED NOT NULL,
  `entry`  INT UNSIGNED NOT NULL,
  `source` VARCHAR(16)  NOT NULL,
  `detail` INT UNSIGNED NOT NULL DEFAULT 0,
  `at`     INT UNSIGNED NOT NULL,
  PRIMARY KEY (`item`),
  KEY `owner` (`owner`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
