-- CoA World 3.1.4: the count a guild order asked (0: any), for the addon's GO line ("5 of 20"). One row a path of a
-- combined order (guild bank, bags, craft, auction house), each with the order's count asked.
ALTER TABLE `coa_world_guild_log` ADD COLUMN `asked` SMALLINT UNSIGNED NOT NULL DEFAULT 0;
