-- CoA Live 3_0_16 : correctif typographique de la banque de nuit (playerbots_coa_night_lines).
-- Apostrophes et guillemets courbes, tirets longs, points de suspension et espaces insécables -> ASCII, que le
-- client 3.3.5 affiche mal. Les accents et les %variables ne sont pas touchés. Rejouable (sans effet la 2e fois).
-- Livré avec la 3_0_09_night (branche de CoA SquidBot), chargé après elle. 45 lignes concernées dans la 3_0_09.
-- La banque LIVE (playerbots_coa_live_lines, jusqu'à 3_0_15) n'en contient aucune : rien à corriger.

CREATE TABLE IF NOT EXISTS `playerbots_coa_night_lines` (
  `id`         INT UNSIGNED      NOT NULL AUTO_INCREMENT,
  `source`     TINYINT UNSIGNED  NOT NULL COMMENT '1 = génération nuit-3-0 (mistral-small, 09/11/2026)',
  `channel`    TINYINT UNSIGNED  NOT NULL COMMENT '1 = /1 général de zone, 3 = /3 monde, 10 = groupe',
  `event`      VARCHAR(24)       NOT NULL,
  `locale`     TINYINT UNSIGNED  NOT NULL COMMENT '0 EN, 2 FR',
  `race`       TINYINT UNSIGNED  NOT NULL DEFAULT 0 COMMENT '0 = toutes',
  `class`      TINYINT UNSIGNED  NOT NULL DEFAULT 0 COMMENT '0 = toutes',
  `level_band` TINYINT UNSIGNED  NOT NULL DEFAULT 0 COMMENT '0 tous, 1 = 1-19, 2 = 20-39, 3 = 40-59, 4 = 60',
  `zone`       SMALLINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'AreaTable, 0 = toutes',
  `thread`     SMALLINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'conversation (0 = ligne seule)',
  `step`       TINYINT UNSIGNED  NOT NULL DEFAULT 0 COMMENT 'rang dans la conversation',
  `speaker`    TINYINT UNSIGNED  NOT NULL DEFAULT 0 COMMENT '1, 2, 3 = locuteurs A, B, C',
  `weight`     TINYINT UNSIGNED  NOT NULL DEFAULT 10,
  `text`       VARCHAR(255)      NOT NULL COMMENT 'emplacements : %player %zone %class %level %item %boss',
  PRIMARY KEY (`id`),
  KEY `k_pick` (`locale`, `channel`, `event`, `level_band`),
  KEY `k_thread` (`source`, `thread`, `step`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='CoA Live : banque de nuit (clé AiPlayerbot.CoaLive.NightBank.Enable)';

UPDATE `playerbots_coa_night_lines` SET `text` =
  REPLACE(REPLACE(REPLACE(REPLACE(REPLACE(REPLACE(REPLACE(REPLACE(REPLACE(REPLACE(REPLACE(REPLACE(`text`,
    _utf8mb4 0xE28098, ''''), _utf8mb4 0xE28099, ''''), _utf8mb4 0xE2809A, ''''), _utf8mb4 0xE2809B, ''''),
    _utf8mb4 0xE2809C, '"'), _utf8mb4 0xE2809D, '"'), _utf8mb4 0xE2809E, '"'),
    _utf8mb4 0xE28093, '-'), _utf8mb4 0xE28094, '-'), _utf8mb4 0xE280A6, '...'),
    _utf8mb4 0xC2A0, ' '), _utf8mb4 0xE280AF, ' ')
WHERE `text` LIKE CONCAT('%', _utf8mb4 0xE28098, '%') OR `text` LIKE CONCAT('%', _utf8mb4 0xE28099, '%')
   OR `text` LIKE CONCAT('%', _utf8mb4 0xE2809A, '%') OR `text` LIKE CONCAT('%', _utf8mb4 0xE2809B, '%')
   OR `text` LIKE CONCAT('%', _utf8mb4 0xE2809C, '%') OR `text` LIKE CONCAT('%', _utf8mb4 0xE2809D, '%')
   OR `text` LIKE CONCAT('%', _utf8mb4 0xE2809E, '%') OR `text` LIKE CONCAT('%', _utf8mb4 0xE28093, '%')
   OR `text` LIKE CONCAT('%', _utf8mb4 0xE28094, '%') OR `text` LIKE CONCAT('%', _utf8mb4 0xE280A6, '%')
   OR `text` LIKE CONCAT('%', _utf8mb4 0xC2A0, '%') OR `text` LIKE CONCAT('%', _utf8mb4 0xE280AF, '%');
