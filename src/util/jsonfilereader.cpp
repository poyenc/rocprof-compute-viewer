// src/util/jsonfilereader.cpp
#include "jsonfilereader.h"

#ifndef QWARNING
#    define QWARNING(cond, msg, action) \
        if (!(cond))                    \
        {                               \
            std::cout << msg << "\n";   \
            action;                     \
        }
#endif

JsonFileReader::JsonFileReader(const std::string& path, bool bWarn)
{
    std::ifstream f(path);
    if (!f.good())
    {
        QWARNING(!bWarn, "Could not parse: " << path, return);
        return;
    }
    try
    {
        data = nlohmann::json::parse(f);
        bValid = true;
    }
    catch (const nlohmann::json::parse_error& e)
    {
        QWARNING(!bWarn, "JSON parse error in " << path << ": " << e.what(), return);
    }
}
