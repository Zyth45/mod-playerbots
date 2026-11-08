-- CoA 3.0: the souls of the bots (contract ACCROCHES-LLM-3.0 §1, CoA IA MIND), read and written by CoaSoul
-- (AiPlayerbot.CoaSoul.Enable). Re-applicable: creates the table only when missing, never touches its rows.
-- persona stays empty in 3.0; the LLM of 3.1 writes it.

CREATE TABLE IF NOT EXISTS `playerbots_coa_soul` (
  `bot_guid`    INT UNSIGNED     NOT NULL PRIMARY KEY,
  `archetype`   TINYINT UNSIGNED NOT NULL COMMENT '0 lazy, 1 quester, 2 wanderer, 3 trader, 4 pvp',
  `pace`        TINYINT UNSIGNED NOT NULL,
  `sociability` TINYINT UNSIGNED NOT NULL,
  `curiosity`   TINYINT UNSIGNED NOT NULL,
  `greed`       TINYINT UNSIGNED NOT NULL,
  `aggression`  TINYINT UNSIGNED NOT NULL,
  `seed`        INT UNSIGNED     NOT NULL,
  `source`      TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 rules, 1 LLM (3.1)',
  `version`     TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `persona`     VARCHAR(255)     NOT NULL DEFAULT '',
  `created_at`  INT UNSIGNED     NOT NULL,
  `updated_at`  INT UNSIGNED     NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='CoA 3.0: souls of the bots';
