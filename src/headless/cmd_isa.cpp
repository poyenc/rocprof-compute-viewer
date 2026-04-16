// SPDX-License-Identifier: MIT
#include "cmd_isa.h"
#include "json_output.h"

namespace Headless
{

int cmdIsa(const HeadlessArgs& args, SessionCache& cache)
{
    auto& code = cache.loadCode();

    int minCycles = 0;
    std::string minCyclesStr = getOption(args.options, "--min-cycles");
    if (!minCyclesStr.empty()) minCycles = std::stoi(minCyclesStr);

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
