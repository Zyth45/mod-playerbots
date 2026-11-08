/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "CoaBotNames.h"

#include "CoaSurnameRules.h"

#include "CharacterCache.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "PlayerbotAIConfig.h"
#include "SharedDefines.h"

#include <unordered_set>
#include <vector>

std::string CoaBotName(std::string const& firstName)
{
    // Mode 2 needs the bot's guid, which a new character has not yet: ApplyCoaBotSurnames, run right after the
    // random bots are created, gives it its family name.
    if (sPlayerbotAIConfig.coaBotSurname != COA_SURNAME_BOT || firstName.find(' ') != std::string::npos)
        return firstName;
    return firstName + " Bot";
}

void ApplyCoaBotSurnames()
{
    // At startup only: a config reload runs this again with the bots online, whose names would
    // then differ from the ones the database and the name cache hold.
    static bool applied = false;
    if (applied)
        return;
    applied = true;

    uint32 const mode = sPlayerbotAIConfig.coaBotSurname;
    std::vector<std::string> const& list = sPlayerbotAIConfig.coaBotSurnameList;

    std::unordered_set<uint32> accounts;
    if (QueryResult result = LoginDatabase.Query("SELECT id FROM account WHERE username LIKE '{}%'",
                                                 sPlayerbotAIConfig.randomBotAccountPrefix))
        do
            accounts.insert(result->Fetch()[0].Get<uint32>());
        while (result->NextRow());
    if (accounts.empty())
        return;

    struct Row { uint32 guid; uint32 account; std::string name; };
    std::vector<Row> rows;
    std::unordered_set<std::string> names;
    if (QueryResult result = CharacterDatabase.Query("SELECT guid, account, name FROM characters"))
        do
        {
            Field* fields = result->Fetch();
            rows.push_back({ fields[0].Get<uint32>(), fields[1].Get<uint32>(), fields[2].Get<std::string>() });
            names.insert(rows.back().name);
        } while (result->NextRow());

    uint32 renamed = 0, skipped = 0;
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    for (Row const& row : rows)
    {
        if (!accounts.count(row.account))
            continue;

        std::string renamedTo = CoaWantedBotName(row.name, mode, row.guid, list);
        if (renamedTo == row.name)
            continue;

        // A player may hold the name already, or the realm refuses it: that bot keeps its own.
        if (names.count(renamedTo) ||
            (mode == COA_SURNAME_FAMILY && ObjectMgr::CheckPlayerName(renamedTo, false) != CHAR_NAME_SUCCESS))
        {
            ++skipped;
            continue;
        }

        names.erase(row.name);
        names.insert(renamedTo);
        sCharacterCache->UpdateCharacterData(ObjectGuid::Create<HighGuid::Player>(row.guid), renamedTo);
        CharacterDatabase.EscapeString(renamedTo);
        trans->Append("UPDATE characters SET name = '{}' WHERE guid = {}", renamedTo, row.guid);
        ++renamed;
    }
    CharacterDatabase.CommitTransaction(trans);

    static char const* const modes[] = { "no surname", "the surname \"Bot\"", "a family name" };
    if (renamed || skipped)
        LOG_INFO("server.loading", ">> Gave {} to {} random bots ({} kept their name: taken or refused)",
                 modes[mode <= COA_SURNAME_FAMILY ? mode : COA_SURNAME_BOT], renamed, skipped);
}
