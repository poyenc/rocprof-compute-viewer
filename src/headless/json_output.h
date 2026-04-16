// SPDX-License-Identifier: MIT
#pragma once

#include <iostream>
#include <string>
#include "json/include/nlohmann/json.hpp"

namespace Headless
{

/// Write pretty-printed JSON to stdout with standard envelope.
inline void writeJson(
    const std::string& command,
    const std::string& uiOutputDir,
    const nlohmann::json& data,
    int offset = -1,
    int limit = -1,
    int total = -1
)
{
    nlohmann::json envelope;
    envelope["command"] = command;
    envelope["ui_output_dir"] = uiOutputDir;
    envelope["data"] = data;

    if (offset >= 0 && limit >= 0 && total >= 0)
    {
        envelope["pagination"]["offset"] = offset;
        envelope["pagination"]["limit"] = limit;
        envelope["pagination"]["total"] = total;
    }

    std::cout << envelope.dump(2) << std::endl;
}

/// Write compact single-line JSON to stdout (NDJSON for interactive mode).
inline void writeJsonCompact(
    const std::string& command,
    const std::string& uiOutputDir,
    const nlohmann::json& data,
    int offset = -1,
    int limit = -1,
    int total = -1
)
{
    nlohmann::json envelope;
    envelope["command"] = command;
    envelope["ui_output_dir"] = uiOutputDir;
    envelope["data"] = data;

    if (offset >= 0 && limit >= 0 && total >= 0)
    {
        envelope["pagination"]["offset"] = offset;
        envelope["pagination"]["limit"] = limit;
        envelope["pagination"]["total"] = total;
    }

    std::cout << envelope.dump(-1) << std::endl;
}

/// Write an error JSON to stderr.
inline void writeError(const std::string& message)
{
    nlohmann::json err;
    err["error"] = message;
    std::cerr << err.dump(-1) << std::endl;
}

} // namespace Headless
