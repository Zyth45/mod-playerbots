/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "Helpers.h"
#include "Util.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

/**
 * Case-insensitive substring search.
 */
char* strstri(char const* haystack, char const* needle)
{
    if (!*needle)
    {
        return (char*)haystack;
    }

    for (; *haystack; ++haystack)
    {
        if (tolower(*haystack) == tolower(*needle))
        {
            char const* h = haystack;
            char const* n = needle;

            for (; *h && *n; ++h, ++n)
            {
                if (tolower(*h) != tolower(*n))
                {
                    break;
                }
            }

            if (!*n)
            {
                return (char*)haystack;
            }
        }
    }

    return 0;
}

/**
 * Trim whitespace from the left side of a string (in place).
 */
std::string& ltrim(std::string& s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int c) { return !std::isspace(c); }));
    return s;
}

/**
 * Trim whitespace from the right side of a string (in place).
 */
std::string& rtrim(std::string& s)
{
    s.erase(std::find_if(s.rbegin(), s.rend(), [](int c) { return !std::isspace(c); }).base(), s.end());
    return s;
}

/**
 * Trim whitespace from both ends of a string (in place).
 */
std::string& trim(std::string& s) { return ltrim(rtrim(s)); }

/**
 * Split a string using a C-string delimiter.
 */
void split(std::vector<std::string>& dest, std::string const str, char const* delim)
{
    char* pTempStr = strdup(str.c_str());
    char* pWord = strtok(pTempStr, delim);

    while (pWord != nullptr)
    {
        dest.push_back(pWord);
        pWord = strtok(nullptr, delim);
    }

    free(pTempStr);
}

/**
 * Split a string using a single character delimiter.
 */
std::vector<std::string>& split(std::string const s, char delim, std::vector<std::string>& elems)
{
    std::stringstream ss(s);
    std::string item;

    while (getline(ss, item, delim))
    {
        elems.push_back(item);
    }

    return elems;
}

/**
 * Split a string using a single character delimiter.
 */
std::vector<std::string> split(std::string const s, char delim)
{
    std::vector<std::string> elems;
    return split(s, delim, elems);
}

std::wstring CoaNameKey(std::string_view name)
{
    std::wstring key;
    if (!Utf8toWStr(name, key))
        return std::wstring();

    wstrToLower(key);
    for (wchar_t& c : key)
        if (c == 0x2018 || c == 0x2019 || c == 0x02BC)
            c = L'\'';

    return key;
}

bool CoaNameIs(std::string_view name, std::wstring const& key)
{
    if (key.empty() || name.size() < key.size())
        return false;

    std::size_t i = 0;
    for (; i < name.size(); ++i)
    {
        unsigned char const b = static_cast<unsigned char>(name[i]);
        if (b >= 0x80)
            break;

        if (i >= key.size() || wchar_t(std::tolower(b)) != key[i])
            return false;
    }

    if (i == name.size())
        return i == key.size();

    std::size_t units = i;
    for (std::size_t j = i; j < name.size(); ++j)
    {
        unsigned char const b = static_cast<unsigned char>(name[j]);
        if ((b & 0xC0) != 0x80)
            units += (sizeof(wchar_t) == 2 && b >= 0xF0) ? 2 : 1;
    }

    if (units != key.size())
        return false;

    return CoaNameKey(name) == key;
}
