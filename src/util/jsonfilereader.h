// src/util/jsonfilereader.h
#pragma once

#include <fstream>
#include <iostream>
#include <string>
#include "json/include/nlohmann/json.hpp"

/// Qt-free JSON file loader. Reads a local JSON file via std::ifstream.
/// This is the shared base for both CLI and GUI JSON loading.
/// The GUI's JsonRequest extends this with QNetwork HTTP support.
class JsonFileReader
{
public:
    explicit JsonFileReader(const std::string& path, bool bWarn = true);
    virtual ~JsonFileReader() = default;

    bool bValid = false;
    nlohmann::json data;

protected:
    JsonFileReader() = default;  // for derived classes that do their own loading
};
