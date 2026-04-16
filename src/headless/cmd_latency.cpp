// SPDX-License-Identifier: MIT
#include "cmd_latency.h"
#include "analysis/latency.hpp"
#include "json_output.h"

#include <sstream>

namespace Headless
{

int cmdLatency(const HeadlessArgs& args, SessionCache& cache)
{
    // Parse options
    std::string typeStr = getOption(args.options, "--type", "vmem");
    std::string cuStr = getOption(args.options, "--cu", "1");
    std::string perfIntervalStr = getOption(args.options, "--perf-interval", "40");
    std::string seStr = getOption(args.options, "--se");

    int targetCu = std::stoi(cuStr);
    int perfInterval = std::stoi(perfIntervalStr);

    // Map type string to counter name
    std::string counterName;
    if (typeStr == "vmem" || typeStr == "VMEM")
        counterName = "SQ_INST_LEVEL_VMEM";
    else if (typeStr == "lds" || typeStr == "LDS")
        counterName = "SQ_INST_LEVEL_LDS";
    else if (typeStr == "smem" || typeStr == "SMEM")
        counterName = "SQ_INST_LEVEL_SMEM";
    else
        counterName = typeStr;  // Allow raw counter name

    // Determine shader engines
    std::vector<int> shaderEngines;
    if (!seStr.empty())
    {
        std::stringstream ss(seStr);
        std::string item;
        while (std::getline(ss, item, ','))
            shaderEngines.push_back(std::stoi(item));
    }
    else
    {
        int numSE = cache.numShaderEngines();
        for (int i = 0; i < numSE; ++i)
            shaderEngines.push_back(i);
    }

    if (shaderEngines.empty())
    {
        writeError("No shader engines found");
        return 1;
    }

    // Load shared data
    auto counterNames = cache.counterNames();
    auto codeMap = cache.buildCodeMap();

    // Create analyzer
    LatencyAnalysis::LatencyAnalyzer analyzer(counterNames, codeMap, counterName, targetCu, perfInterval);

    // Process each shader engine
    for (int se : shaderEngines)
    {
        std::string perfFile = cache.baseDir() + "se" + std::to_string(se) + "_perfcounter.json";
        auto waveFilePaths = LatencyAnalysis::LatencyAnalyzer::collectWaveFilePaths(cache.baseDir(), se);

        if (waveFilePaths.empty()) continue;

        analyzer.analyzeFiles(perfFile, waveFilePaths);
    }

    // Get results
    auto results = analyzer.getResults();

    // Build output
    nlohmann::json instructions = nlohmann::json::array();
    for (auto& [codeIndex, instr] : results)
    {
        auto stats = LatencyAnalysis::computeLatencyStats(instr.latencies, perfInterval);

        nlohmann::json instrJson;
        instrJson["code_index"] = codeIndex;
        instrJson["code"] = instr.code;
        instrJson["stats"]["mean"] = stats.mean;
        instrJson["stats"]["stddev"] = stats.stdDev;
        instrJson["stats"]["error"] = stats.error;
        instrJson["stats"]["count"] = stats.count;
        instrJson["stats"]["mean_issue"] = stats.meanIssue;
        instrJson["stats"]["mean_stall"] = stats.meanStall;
        instructions.push_back(std::move(instrJson));
    }

    int total = static_cast<int>(instructions.size());

    // Apply pagination
    nlohmann::json data;
    data["counter_type"] = counterName;
    data["target_cu"] = targetCu;
    data["perf_interval"] = perfInterval;
    data["shader_engines"] = shaderEngines;

    if (args.offset > 0 || args.limit > 0)
    {
        int off = std::max(0, args.offset);
        int lim = args.limit > 0 ? args.limit : total;
        off = std::min(off, total);
        lim = std::min(lim, total - off);

        nlohmann::json paginated = nlohmann::json::array();
        for (int i = off; i < off + lim; ++i)
            paginated.push_back(instructions[i]);

        data["instructions"] = std::move(paginated);

        if (args.compact)
            writeJsonCompact("latency", args.uiOutputDir, data, off, lim, total);
        else
            writeJson("latency", args.uiOutputDir, data, off, lim, total);
    }
    else
    {
        data["instructions"] = std::move(instructions);

        if (args.compact)
            writeJsonCompact("latency", args.uiOutputDir, data);
        else
            writeJson("latency", args.uiOutputDir, data);
    }

    return 0;
}

} // namespace Headless
