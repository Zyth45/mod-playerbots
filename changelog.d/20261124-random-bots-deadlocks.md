### No more database deadlocks when thousands of bots log in
Each bot's scheduled events were saved by deleting then inserting a row, and thousands of bots logging in at once made those writes deadlock. They are now saved in one statement, on a table kept to one row per bot and event (new SQL update; the duplicates are cleaned first).
