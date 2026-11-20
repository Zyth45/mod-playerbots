### No more server stop after a simulated battleground
With simulated battlegrounds on (AiPlayerbot.CoaWorld.BgSim.Enable = 1), the server stopped within the hour with "Unknown column 'c.id1'" the first time a bot visited the honor shop after a match: the query still used the old name of a creature column renamed by the core. It now reads the column the database has.
