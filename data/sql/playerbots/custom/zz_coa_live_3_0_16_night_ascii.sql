-- CoA Live 3_0_16 : correctif typographique de la banque de nuit (playerbots_coa_night_lines).
-- Rendu inoffensif le 12/11 (avant toute publication) : son UPDATE ... LIKE sur des littéraux binaires échouait
-- (ERROR 1267, mélange de collations) quand la connexion n'est pas en utf8mb4_0900_ai_ci, comme sur un repack 2.9.5.
-- La 3_0_17_night, chargée juste après, recharge toute la source 1 déjà convertie en ASCII : ce correctif ne sert
-- plus. Le fichier garde seulement la création de la table, sans aucune comparaison de texte. Rejouable.

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
