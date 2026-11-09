/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COADBCHECK_H
#define PLAYERBOTS_COADBCHECK_H

#include "DatabaseEnv.h"
#include "Log.h"
#include "QueryResult.h"

#include <initializer_list>
#include <string>

// The core aborts the server on a query naming a missing table or column (MySQLConnection::HandleMySQLErrno,
// errors 1146 and 1054), even with the options at 0. Before the first read or write of a CoA table, check it here:
// information_schema never fails. A missing piece logs one clear error and the caller turns its brick off.
namespace CoaDbCheck
{
// True when the playerbots database has this table with all these columns.
inline bool Has(std::string const& table, std::initializer_list<char const*> columns)
{
    std::string in;
    for (char const* c : columns)
        in += std::string(in.empty() ? "'" : ",'") + c + "'";
    QueryResult r = PlayerbotsDatabase.Query(
        "SELECT COUNT(*) FROM information_schema.columns WHERE table_schema = DATABASE() AND table_name = '{}'{}",
        table, in.empty() ? std::string() : " AND column_name IN (" + in + ")");
    uint64 const found = r ? r->Fetch()[0].Get<uint64>() : 0;
    return columns.size() ? found == columns.size() : found > 0;
}

// Has(), with the error said once for the brick that is turned off.
inline bool Require(char const* brick, std::string const& table, std::initializer_list<char const*> columns)
{
    if (Has(table, columns))
        return true;
    LOG_ERROR("server.loading",
              ">> {}: table {} missing or out of date (data/sql/playerbots/custom not applied): turned off, the server "
              "starts without it.",
              brick, table);
    return false;
}
}  // namespace CoaDbCheck

#endif
