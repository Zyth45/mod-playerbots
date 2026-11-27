-- CoA 3.2.2: a bot's strategy lists ("co", "nc", "dead") are saved in playerbots_db_store.value, a varchar(255). The
-- non-combat strategies the module may set (AiFactory::AddDefaultNonCombatStrategies and the CoA ones: 67 names, 660
-- characters written out as "+name,") pass 255 characters for a well-equipped player's bot: the write failed (strict
-- mode) or was cut, and the bot lost or mangled its strategies at its next login. TEXT holds any of them.
ALTER TABLE `playerbots_db_store` MODIFY `value` TEXT NULL;
