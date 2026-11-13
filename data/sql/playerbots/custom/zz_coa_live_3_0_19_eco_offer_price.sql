-- CoA 3.0.1 Economy: a bot's Trade offer (WTS) answers a price question with its price and waits for a clear yes
-- (wh_offer_price, whisper). Added after zz_coa_live_3_0_19.sql; the next zz_coa_live_<version>.sql keeps these rows.
-- Replayable: replaces only its own situation.
DELETE FROM `playerbots_coa_live_lines` WHERE `situation` = 'wh_offer_price';
INSERT INTO `playerbots_coa_live_lines` (`situation`,`channel`,`locale`,`level_band`,`emote`,`text`,`w_lazy`,`w_quester`,`w_wanderer`,`w_trader`,`w_pvp`,`rare`) VALUES
('wh_offer_price',6,0,0,0,'%item is %price, want it?',10,10,10,18,10,0),
('wh_offer_price',6,0,0,0,'%price for %item. say yes and i''ll mail it COD',10,10,10,18,10,0),
('wh_offer_price',6,0,0,0,'%price. interested?',10,10,10,10,10,0),
('wh_offer_price',6,0,0,0,'asking %price for it, can send COD if you want',10,10,10,18,10,0),
('wh_offer_price',6,0,0,0,'%item? %price, lmk',10,10,10,10,10,0),
('wh_offer_price',6,0,0,0,'%price, deal?',10,10,10,18,10,0),
('wh_offer_price',6,2,0,0,'%item c''est %price, ça te dit ?',10,10,10,18,10,0),
('wh_offer_price',6,2,0,0,'%price pour %item. dis oui et je l''envoie en CR',10,10,10,18,10,0),
('wh_offer_price',6,2,0,0,'%price. intéressé ?',10,10,10,10,10,0),
('wh_offer_price',6,2,0,0,'j''en demande %price, je peux l''envoyer en CR si tu veux',10,10,10,18,10,0),
('wh_offer_price',6,2,0,0,'%item ? %price, dis-moi',10,10,10,10,10,0);
DELETE FROM `playerbots_coa_live_rules` WHERE `situation` = 'wh_offer_price';
INSERT INTO `playerbots_coa_live_rules` VALUES ('wh_offer_price',100,2000,7000,0,0,0,1);
