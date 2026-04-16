// SPDX-License-Identifier: MIT
#include "cmd_perfcounters.h"
#include "json_output.h"
#include "util/jsonfilereader.h"

namespace Headless
{

int cmdPerfcounters(const HeadlessArgs& args, SessionCache& cache)
{
    // Parse filters
    int filterSE = -1;
    std::string seStr = getOption(args.options, "--se");
    if (!seStr.empty()) filterSE = std::stoi(seStr);

    int numSE = cache.numShaderEngines();
    auto counterNames = cache.counterNames();

    nlohmann::json engines = nlohmann::json::array();

    for (int se = 0; se < numSE; ++se)
    {
        if (filterSE >= 0 && se != filterSE) continue;

        std::string perfFile = cache.baseDir() + "se" + std::to_string(se) + "_perfcounter.json";

        JsonFileReader reader(perfFile, false);
        if (!reader.bValid) continue;

        nlohmann::json engineJson;
        engineJson["se"] = se;

        nlohmann::json samples = nlohmann::json::array();
        if (reader.data.contains("data"))
        {
            for (auto& row : reader.data["data"])
            {
                // Each row: [timestamp, c1, c2, c3, c4, cu, bank]
                if (row.size() < 7) continue;

                nlohmann::json sample;
                sample["timestamp"] = row[0];
                sample["c1"] = row[1];
                sample["c2"] = row[2];
                sample["c3"] = row[3];
                sample["c4"] = row[4];
                sample["cu"] = row[5];
                sample["bank"] = row[6];
                samples.push_back(std::move(sample));
            }
        }

        engineJson["samples"] = std::move(samples);
        engineJson["counter_names"] = counterNames;
        engines.push_back(std::move(engineJson));
    }

    nlohmann::json data;
    data["engines"] = std::move(engines);

    if (args.compact)
        writeJsonCompact("perfcounters", args.uiOutputDir, data);
    else
        writeJson("perfcounters", args.uiOutputDir, data);

    return 0;
}

} // namespace Headless
