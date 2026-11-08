/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "CoaBotNames.h"

#include "CoaSurnameRules.h"

#include "ArenaTeam.h"
#include "ArenaTeamMgr.h"
#include "CharacterCache.h"
#include "DatabaseEnv.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Log.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "PlayerbotAIConfig.h"
#include "SharedDefines.h"

#include <unordered_map>
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

    std::vector<CoaNameRow> rows;
    if (QueryResult result = CharacterDatabase.Query("SELECT guid, account, name FROM characters"))
        do
        {
            Field* fields = result->Fetch();
            rows.push_back({ fields[0].Get<uint32>(), fields[2].Get<std::string>(),
                             accounts.count(fields[1].Get<uint32>()) != 0 });
        } while (result->NextRow());

    // Mode 2 gives names nobody chose: the realm checks them as it checks a player's (reserved, profanity...).
    uint32 skipped = 0;
    std::vector<CoaRename> const renames = CoaPlanBotRenames(
        rows, mode, list,
        [mode](std::string const& name)
        { return mode != COA_SURNAME_FAMILY || ObjectMgr::CheckPlayerName(name, false) == CHAR_NAME_SUCCESS; },
        &skipped);

    // Friends, ignore lists, mail, guilds and arena teams hold the guid: only the names change. The name cache is
    // what /w, /invite, .summon and the name queries read; guild rosters and arena teams were loaded before this
    // and keep a copy of the name, set here too (a guild also takes it again when the bot logs in).
    std::unordered_map<uint32, std::string> newNames;
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    for (CoaRename const& rename : renames)
    {
        ObjectGuid const guid = ObjectGuid::Create<HighGuid::Player>(static_cast<ObjectGuid::LowType>(rename.guid));
        sCharacterCache->UpdateCharacterData(guid, rename.to);
        if (ObjectGuid::LowType const guildId = sCharacterCache->GetCharacterGuildIdByGuid(guid))
            if (Guild* guild = sGuildMgr->GetGuildById(guildId))
                if (Guild::Member* member = guild->GetMember(guid))
                    member->SetStats(rename.to, member->GetLevel(), member->GetClass(), member->GetGender(),
                                     member->GetZoneId(), member->GetAccountId());
        newNames[static_cast<uint32>(rename.guid)] = rename.to;

        std::string escaped = rename.to;
        CharacterDatabase.EscapeString(escaped);
        trans->Append("UPDATE characters SET name = '{}' WHERE guid = {}", escaped, rename.guid);
    }
    CharacterDatabase.CommitTransaction(trans);

    if (!newNames.empty())
        for (auto const& entry : sArenaTeamMgr->GetArenaTeams())
            for (ArenaTeamMember& member : entry.second->GetMembers())
                if (auto it = newNames.find(member.Guid.GetCounter()); it != newNames.end())
                    member.Name = it->second;

    uint32 const renamed = static_cast<uint32>(renames.size());
    static char const* const modes[] = { "no surname", "the surname \"Bot\"", "a family name" };
    if (renamed || skipped)
        LOG_INFO("server.loading", ">> Gave {} to {} random bots ({} kept their name: taken or refused)",
                 modes[mode <= COA_SURNAME_FAMILY ? mode : COA_SURNAME_BOT], renamed, skipped);
}
