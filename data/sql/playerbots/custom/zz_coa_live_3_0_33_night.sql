-- CoA Live 3_0_33_night : retrait des métiers Calligraphie et Joaillerie de la banque de nuit (3.3.1, décision du 25/11).
-- Remplace la seule ligne qui en parlait (dgn_loot_self FR, source 1) par une phrase sans métier ; même nombre de lignes.
-- Se charge après zz_coa_live_3_0_09_night.sql (ordre alphabétique) ; rejouable : l'UPDATE ne touche plus rien au 2e passage.
-- On ne modifie jamais un SQL publié : ce fichier corrige la table, pas le fichier 3_0_09_night.

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

UPDATE `playerbots_coa_night_lines` SET `text` = 'j''ai drop %item, jsp encore quoi en faire'
WHERE `source` = 1 AND `locale` = 2 AND `event` = 'dgn_loot_self' AND `text` = 'j''ai drop %item, jsp si je vais pas chez le joaillier';
