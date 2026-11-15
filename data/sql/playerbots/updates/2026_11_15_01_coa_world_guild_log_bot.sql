-- CoA World: a bot's name with its suffix ("Tydadrohr Bot", 13) did not fit the 12 of `bot`: the row of an order
-- served by such a bot was refused, and the addon's history showed the order as "nobody could".
ALTER TABLE `coa_world_guild_log` MODIFY `bot` VARCHAR(32) NOT NULL DEFAULT '';
