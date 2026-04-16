// SPDX-License-Identifier: MIT
#pragma once

#include <fstream>
#include <string>
#include <utility>
#include <vector>
#include "json/include/nlohmann/json.hpp"

namespace TokenTypeMap
{

constexpr int MATRIX_TYPE_INDEX = 6;

inline std::vector<std::pair<std::string, int>> defaults()
{
    static const std::vector<std::pair<std::string, int>> name_to_index = {
        {"IDLE", 0},    {"SCALAR", 1}, {"VECTOR", 2},  {"VMEM", 3},
        {"LDS", 4},     {"EXPORT", 5}, {"MATRIX", 6},  {"BARRIER", 7},
        {"JUMP", 8},    {"OTHER", 9},  {"SPECIAL", 10}, {"SMEM", 11},
        {"FLAT", 12},
    };

    auto find_index = [&](const std::string& name) -> int
    {
        for (auto& [n, i] : name_to_index)
            if (n == name) return i;
        return -1;
    };

    nlohmann::json key;
    key["v_mfma"] = "MATRIX";
    key["v_smfma"] = "MATRIX";
    key["v_wmma"] = "MATRIX";
    key["v_swmma"] = "MATRIX";

    try
    {
        std::ifstream ifs("token_def.json");
        if (ifs.is_open())
        {
            nlohmann::json new_asm;
            ifs >> new_asm;
            if (new_asm.contains("asmkeys"))
                for (auto& [isa, value] : new_asm["asmkeys"].items())
                    key[isa] = value;
        }
    }
    catch (...)
    {}

    std::vector<std::pair<std::string, int>> result;
    for (auto& [match, inst] : key.items())
    {
        int idx = find_index(inst.get<std::string>());
        if (idx >= 0) result.push_back({match, idx});
    }
    return result;
}

} // namespace TokenTypeMap
