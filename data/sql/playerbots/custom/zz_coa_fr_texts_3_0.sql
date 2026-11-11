-- CoA 3.0: French texts of ai_playerbot_texts whose placeholders were translated or broken ("%Quête" for
-- %quest_link, "%nom_de_l'instance", "%%victim_name"...): the bot said them as written. Each FR text gets back the
-- placeholders of its EN text. Found by a check of every FR text against its EN text (CoA SquidBot, 09/11/2026).
-- Only these rows' text_loc2 change; nothing else.

UPDATE `ai_playerbot_texts` SET `text_loc2` = 'Quête %quest_link acceptée dans %zone_name. Voyons ce que cela nous réserve !' WHERE `id` = 135;
UPDATE `ai_playerbot_texts` SET `text_loc2` = 'Je viens de vaincre l''élite rare %victim_name.' WHERE `id` = 278;
UPDATE `ai_playerbot_texts` SET `text_loc2` = 'Besoin d''un %my_role pour %instance_name !' WHERE `id` = 456;
UPDATE `ai_playerbot_texts` SET `text_loc2` = 'Quelqu''un pour faire des quêtes %faction avec moi ?' WHERE `id` = 699;
UPDATE `ai_playerbot_texts` SET `text_loc2` = 'Quelqu''un farm la réputation %faction pour atteindre %rep_level ?' WHERE `id` = 703;
UPDATE `ai_playerbot_texts` SET `text_loc2` = 'Vous pensez quoi de mon build ? %my_role' WHERE `id` = 780;
UPDATE `ai_playerbot_texts` SET `text_loc2` = '%item_formatted_link à %cost_gold, ça vous tente ? Envoyez-moi un message privé !' WHERE `id` = 1065;
UPDATE `ai_playerbot_texts` SET `text_loc2` = '%other_name, je suis partant pour %quest_links, je suis à %zone_name en ce moment' WHERE `id` = 1099;
UPDATE `ai_playerbot_texts` SET `text_loc2` = '%other_name, je suis partant pour %quest_links, je suis %my_role' WHERE `id` = 1100;
UPDATE `ai_playerbot_texts` SET `text_loc2` = 'J''ai déjà terminé %quest' WHERE `id` = 1289;
UPDATE `ai_playerbot_texts` SET `text_loc2` = 'J''ai accepté %quest' WHERE `id` = 1294;
UPDATE `ai_playerbot_texts` SET `text_loc2` = 'Quête %quest disponible' WHERE `id` = 1296;
UPDATE `ai_playerbot_texts` SET `text_loc2` = 'Je n''en peux plus ! Roi des Gobelins ! Roi des Gobelins ! Où que tu sois ! Emmène cette cible loin de moi !' WHERE `id` = 1539;
