-- CoA 3.0 LIVE : phrases et émotes des bots par situation (règles), écrites à la main, aucun LLM.
-- Version 3_0_13. Fichier publié = jamais modifié ; la version suivante va dans un NOUVEAU fichier zz_coa_live_<version>.sql
-- qui refait son propre DELETE + INSERT. À placer dans data/sql/playerbots/custom/ (réappliqué par le DBUpdater).
-- Fichier généré : ne pas le modifier à la main.
-- Base acore_playerbots. Rejouable : crée nos tables si absentes, puis remplace leur seul contenu.
-- Ne touche à aucune autre table (ni ai_playerbot_texts, ni la banque 2.7/2.8).
-- Règles seules : trade_craft_lfw passe à 100 % (CraftOrders.AdvertiseMinutes donne le rythme) ; les phrases ne changent pas.

CREATE TABLE IF NOT EXISTS `playerbots_coa_live_lines` (
  `id`         INT UNSIGNED      NOT NULL AUTO_INCREMENT,
  `situation`  VARCHAR(32)       NOT NULL COMMENT 'voir playerbots_coa_live_rules',
  `channel`    TINYINT UNSIGNED  NOT NULL COMMENT '0 dire/émote, 1 Général, 2 Commerce, 4 Recherche de groupe, 5 guilde, 6 chuchotement, 10 groupe',
  `locale`     TINYINT UNSIGNED  NOT NULL DEFAULT 0 COMMENT '0 = anglais, 2 = français (comme BotTextLocale)',
  `level_band` TINYINT UNSIGNED  NOT NULL DEFAULT 0 COMMENT '0 = tout, 1 = 1-19, 2 = 20-39, 3 = 40-59, 4 = 60',
  `emote`      SMALLINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'EmotesText id joué avec la phrase ; texte vide = émote seule',
  `text`       VARCHAR(255)      NOT NULL DEFAULT '' COMMENT 'emplacements : %player %zone %subzone %city %class %level %item %price %dungeon %need %role %boss %guild %place',
  `w_lazy`     TINYINT UNSIGNED  NOT NULL DEFAULT 10,
  `w_quester`  TINYINT UNSIGNED  NOT NULL DEFAULT 10,
  `w_wanderer` TINYINT UNSIGNED  NOT NULL DEFAULT 10,
  `w_trader`   TINYINT UNSIGNED  NOT NULL DEFAULT 10,
  `w_pvp`      TINYINT UNSIGNED  NOT NULL DEFAULT 10,
  `rare`       TINYINT UNSIGNED  NOT NULL DEFAULT 0 COMMENT '1 = réplique rare (humour) : tirée à RareChance seulement',
  PRIMARY KEY (`id`),
  KEY `idx_pick` (`situation`, `locale`, `level_band`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='CoA 3.0 LIVE : phrases et émotes par situation';

CREATE TABLE IF NOT EXISTS `playerbots_coa_live_rules` (
  `situation`       VARCHAR(32)       NOT NULL,
  `chance_pct`      TINYINT UNSIGNED  NOT NULL COMMENT 'chance qu''un bot réagisse',
  `delay_min_ms`    SMALLINT UNSIGNED NOT NULL COMMENT 'réflexion avant de taper (le temps de frappe s''y ajoute)',
  `delay_max_ms`    SMALLINT UNSIGNED NOT NULL,
  `bot_cd_s`        SMALLINT UNSIGNED NOT NULL COMMENT 'recharge de cette situation pour un même bot',
  `scope_cd_s`      SMALLINT UNSIGNED NOT NULL COMMENT 'écart mini entre deux réactions dans le même canal ou au même endroit',
  `scope_per_10min` TINYINT UNSIGNED  NOT NULL COMMENT 'plafond de phrases de bots par canal ou lieu sur 10 min (0 = aucun)',
  `need_player`     TINYINT UNSIGNED  NOT NULL DEFAULT 1 COMMENT '1 = seulement si un vrai joueur entend',
  PRIMARY KEY (`situation`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='CoA 3.0 LIVE : cadence par situation';

CREATE TABLE IF NOT EXISTS `playerbots_coa_live_intents` (
  `locale`   TINYINT UNSIGNED NOT NULL,
  `priority` TINYINT UNSIGNED NOT NULL COMMENT 'testées de la plus petite à la plus grande ; la 1re qui matche gagne',
  `intent`   VARCHAR(16)      NOT NULL COMMENT 'bot, group, where, trade, help, thanks, hello (sinon fallback)',
  `keywords` VARCHAR(255)     NOT NULL COMMENT 'mots ou expressions en minuscules, séparés par des virgules, cherchés comme mots entiers (pas de regex)',
  PRIMARY KEY (`locale`, `priority`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='CoA 3.0 LIVE : intentions des chuchotements reçus';

CREATE TABLE IF NOT EXISTS `playerbots_coa_live_slang` (
  `locale` TINYINT UNSIGNED NOT NULL,
  `full`   VARCHAR(32)      NOT NULL COMMENT 'mots entiers, en minuscules',
  `short`  VARCHAR(16)      NOT NULL,
  PRIMARY KEY (`locale`, `full`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='CoA 3.0 LIVE : abréviations de joueur';


DELETE FROM `playerbots_coa_live_rules`;
INSERT INTO `playerbots_coa_live_rules` VALUES
('greet_pass',25,600,2500,600,20,6,1),
('levelup_seen',40,800,3000,60,5,6,1),
('levelup_self',60,300,1500,60,5,6,1),
('duel_won',70,800,2500,30,5,6,1),
('duel_lost',70,800,2500,30,5,6,1),
('buff_received',35,600,2500,300,10,6,1),
('rez_received',80,1000,3500,60,10,6,1),
('boss_pull',30,300,1200,120,60,6,1),
('inn_idle',10,1500,6000,900,120,4,1),
('city_idle',5,1500,6000,1200,180,4,1),
('general_chat',10,1500,6000,1800,150,5,1),
('trade_wts',10,1500,6000,1800,120,4,1),
('trade_wtb',8,1500,6000,1800,120,4,1),
('trade_chat',5,1500,6000,1800,240,3,1),
('lfg_lfm',8,1500,6000,1800,180,4,1),
('lfg_lfg',6,1500,6000,1800,180,4,1),
('guild_login',35,2000,8000,300,30,8,1),
('guild_self_login',30,3000,9000,3600,60,8,1),
('guild_levelup',50,1500,6000,60,10,8,1),
('guild_loot',35,1500,6000,300,30,8,1),
('guild_self_loot',40,1000,4000,600,30,8,1),
('guild_idle',5,2000,8000,3600,600,3,1),
('guild_logout',40,800,3000,3600,30,8,1),
('wh_bot',100,2500,8000,0,0,0,1),
('wh_group_yes',100,2000,6000,0,0,0,1),
('wh_group_no',100,2000,6000,0,0,0,1),
('wh_where_known',100,3000,9000,0,0,0,1),
('wh_where_unknown',100,2000,7000,0,0,0,1),
('wh_hello',100,1500,5000,0,0,0,1),
('wh_thanks',100,1000,4000,0,0,0,1),
('wh_trade',100,2000,7000,0,0,0,1),
('wh_help',100,2000,7000,0,0,0,1),
('wh_offer_cod',100,2000,7000,0,0,0,1),
('wh_offer_gone',100,2000,7000,0,0,0,1),
('wh_fallback',80,2000,9000,0,0,0,1),
('inv_rally',30,1500,6000,900,60,6,1),
('inv_fight',15,200,1200,60,15,8,1),
('inv_won',40,2000,8000,600,30,6,1),
('rush_heard',25,1500,7000,1800,120,4,1),
('rush_node',15,500,2500,300,60,4,1),
('dsim_lfm',100,1000,5000,600,120,4,1),
('dsim_back_drop',60,2000,8000,600,60,6,1),
('dsim_back_plain',30,2000,8000,600,60,6,1),
('pvp_skirmish',25,1000,4000,900,60,4,1),
('pvp_capital',40,1000,5000,600,60,6,1),
('travel_wait',15,2000,8000,1800,120,3,1),
('travel_arrive',10,500,2500,1800,120,3,1),
('recovery_home',20,1000,4000,3600,300,2,1),
('sys_inv_warning',100,0,0,0,0,0,1),
('sys_inv_start',100,0,0,0,0,0,1),
('sys_inv_won',100,0,0,0,0,0,1),
('sys_inv_lost',100,0,0,0,0,0,1),
('sys_rush_start',100,0,0,0,0,0,1),
('sys_rush_end',100,0,0,0,0,0,1),
('sys_skirmish',100,0,0,0,0,0,1),
('sys_capital',100,0,0,0,0,0,1),
('guild_ding',70,1000,4000,600,30,8,1),
('guild_gift',100,1500,5000,3600,60,4,1),
('guild_recruit',100,0,0,0,0,0,1),
('guild_motd',100,0,0,0,0,0,0),
('craft_order_ack',100,2000,5000,0,0,0,1),
('craft_order_sent',100,6000,9000,0,0,0,1),
('guild_greet_reply',100,1500,6000,0,0,0,0),
('guild_welcome',100,1500,6000,0,0,0,0),
('charter_ask',100,500,3000,0,0,0,0),
('charter_ask_player',100,500,2500,0,0,0,1),
('charter_sign',100,800,3000,0,0,0,0),
('charter_refuse',100,800,3000,0,0,0,1),
('charter_founded',100,1000,4000,0,0,0,0),
('guild_order_share',100,1500,5000,0,0,0,1),
('guild_order_craft',100,1500,5000,0,0,0,1),
('guild_order_ah',100,1500,5000,0,0,0,1),
('guild_order_none',100,1500,5000,0,0,0,1),
('guild_gear_gift',100,1500,5000,0,0,0,1),
('trade_craft',8,1500,6000,1800,120,4,1),
('trade_craft_lfw',100,1500,6000,3600,180,3,1),
('wh_craft_quote',100,2500,8000,0,0,0,1),
('wh_craft_cod',100,2000,7000,0,0,0,1),
('wh_craft_cannot',100,2000,7000,0,0,0,1),
('wh_craft_busy',100,2000,7000,0,0,0,1),
('social_invite',100,1500,5000,0,0,0,1),
('social_rez',100,300,1500,0,0,0,1),
('social_help',60,300,1200,0,0,0,1),
('social_buff',40,500,2000,0,0,0,1);
