/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_ECONOMY_DB_H
#define COA_ECONOMY_DB_H

#include "DatabaseEnv.h"
#include "QueryResult.h"

#include <initializer_list>
#include <string>

// CoA Economy: the core aborts the server on a query naming a missing table or column (errors 1146 and 1054), even
// with the options at 0. Every CoA Economy table is checked here, through information_schema (which never fails),
// before its first read or write; a missing table or column turns the brick off.
namespace CoaEconomy::Db
{
    // True when the playerbots database has this table with all these columns.
    inline bool HasTable(std::string const& table, std::initializer_list<char const*> columns)
    {
        std::string in;
        for (char const* column : columns)
            in += std::string(in.empty() ? "'" : ",'") + column + "'";
        QueryResult result = PlayerbotsDatabase.Query(
            "SELECT COUNT(*) FROM information_schema.columns WHERE table_schema = DATABASE() AND table_name = '{}' "
            "AND column_name IN ({})", table, in);
        return result && result->Fetch()[0].Get<uint64>() == columns.size();
    }
}

#endif
