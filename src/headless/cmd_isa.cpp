// SPDX-License-Identifier: MIT
#include "cmd_isa.h"
#include "json_output.h"
#include <algorithm>

namespace Headless
{

int cmdIsa(const HeadlessArgs& args, SessionCache& cache)
{
    int minCycles = 0;
    std::string minCyclesStr = getOption(args.options, "--min-cycles");
    if (!minCyclesStr.empty()) minCycles = std::stoi(minCyclesStr);

    std::string sortField = getOption(args.options, "--sort");
    int topN = 0;
    std::string topStr = getOption(args.options, "--top");
    if (!topStr.empty()) topN = std::stoi(topStr);

    // Validate sort field before loading data
    if (!sortField.empty())
    {
        static const std::vector<std::string> validFields = {
            "cycles", "hitcount", "stall", "idle", "pcsamples", "pcstalls"
        };
        if (std::find(validFields.begin(), validFields.end(), sortField) == validFields.end())
        {
            writeError("Invalid sort field '" + sortField + "'. Valid fields: cycles, hitcount, stall, idle, pcsamples, pcstalls");
            return 1;
        }
    }

    auto& code = cache.loadCode();

    // Build filtered instruction list
    nlohmann::json instructions = nlohmann::json::array();
    for (auto& c : code)
    {
        if (minCycles > 0 && c.line->latency_sum < minCycles) continue;

        nlohmann::json inst;
        inst["index"] = c.line->index.load();
        inst["opcode"] = c.line->inst;
        inst["address"] = c.line->addr;
        inst["source"] = c.line->cppline;
        inst["hitcount"] = c.line->hitcount;
        inst["cycles"] = c.line->latency_sum;
        inst["idle"] = c.line->idle_sum;
        inst["stall"] = c.line->stall_sum;
        inst["pcsamples"] = c.line->pcsamples;
        inst["pcstalls"] = c.line->pcstalls;

        if (!c.line->stallreasons.empty())
        {
            nlohmann::json reasons = nlohmann::json::array();
            for (auto v : c.line->stallreasons) reasons.push_back(v);
            inst["stall_reasons"] = reasons;
        }

        instructions.push_back(std::move(inst));
    }

    // Apply sorting
    if (!sortField.empty())
    {
        std::sort(instructions.begin(), instructions.end(),
            [&sortField](const nlohmann::json& a, const nlohmann::json& b) {
                return a.value(sortField, 0) > b.value(sortField, 0);
            });
    }

    // Apply top-N (before pagination)
    if (topN > 0 && topN < static_cast<int>(instructions.size()))
    {
        instructions.erase(instructions.begin() + topN, instructions.end());
    }

    int total = static_cast<int>(instructions.size());

    // Apply pagination
    if (args.offset > 0 || args.limit > 0)
    {
        int off = std::max(0, args.offset);
        int lim = args.limit > 0 ? args.limit : total;
        off = std::min(off, total);
        lim = std::min(lim, total - off);

        nlohmann::json paginated = nlohmann::json::array();
        for (int i = off; i < off + lim; ++i)
            paginated.push_back(instructions[i]);

        nlohmann::json data;
        data["instructions"] = std::move(paginated);

        if (args.compact)
            writeJsonCompact("isa", args.uiOutputDir, data, off, lim, total);
        else
            writeJson("isa", args.uiOutputDir, data, off, lim, total);
    }
    else
    {
        nlohmann::json data;
        data["instructions"] = std::move(instructions);

        if (args.compact)
            writeJsonCompact("isa", args.uiOutputDir, data);
        else
            writeJson("isa", args.uiOutputDir, data);
    }

    return 0;
}

} // namespace Headless
