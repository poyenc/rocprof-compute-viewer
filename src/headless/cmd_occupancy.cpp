// SPDX-License-Identifier: MIT
#include "cmd_occupancy.h"
#include "json_output.h"

namespace Headless
{

int cmdOccupancy(const HeadlessArgs& args, SessionCache& cache)
{
    auto& occupancy = cache.loadOccupancy();

    // Parse filters
    int filterSE = -1;
    int filterCU = -1;
    std::string seStr = getOption(args.options, "--se");
    std::string cuStr = getOption(args.options, "--cu");
    std::string levelStr = getOption(args.options, "--level");
    if (!seStr.empty()) filterSE = std::stoi(seStr);
    if (!cuStr.empty()) filterCU = std::stoi(cuStr);

    // Extract events from occupancy data
    nlohmann::json events = nlohmann::json::array();

    if (occupancy.contains("data"))
    {
        // Occupancy data: each entry is [time, cu, simd, slot, enable, kernel_id]
        for (auto& entry : occupancy["data"])
        {
            if (entry.size() < 6) continue;

            int cu = entry[1].get<int>();
            // SE is typically derived from the file or from top-level metadata
            // For single-file occupancy.json, SE is not per-event
            if (filterCU >= 0 && cu != filterCU) continue;

            nlohmann::json ev;
            ev["time"] = entry[0];
            ev["cu"] = cu;
            ev["simd"] = entry[2];
            ev["slot"] = entry[3];
            ev["enable"] = entry[4];
            ev["kernel_id"] = entry[5];
            events.push_back(std::move(ev));
        }
    }
    else if (occupancy.contains("events"))
    {
        // Alternative format
        for (auto& [seKey, seData] : occupancy["events"].items())
        {
            int se = std::stoi(seKey);
            if (filterSE >= 0 && se != filterSE) continue;

            for (auto& entry : seData)
            {
                if (entry.size() < 6) continue;

                int cu = entry[1].get<int>();
                if (filterCU >= 0 && cu != filterCU) continue;

                nlohmann::json ev;
                ev["se"] = se;
                ev["time"] = entry[0];
                ev["cu"] = cu;
                ev["simd"] = entry[2];
                ev["slot"] = entry[3];
                ev["enable"] = entry[4];
                ev["kernel_id"] = entry[5];
                events.push_back(std::move(ev));
            }
        }
    }

    int total = static_cast<int>(events.size());

    // Apply pagination
    if (args.offset > 0 || args.limit > 0)
    {
        int off = std::max(0, args.offset);
        int lim = args.limit > 0 ? args.limit : total;
        off = std::min(off, total);
        lim = std::min(lim, total - off);

        nlohmann::json paginated = nlohmann::json::array();
        for (int i = off; i < off + lim; ++i)
            paginated.push_back(events[i]);

        nlohmann::json data;
        data["events"] = std::move(paginated);

        if (args.compact)
            writeJsonCompact("occupancy", args.uiOutputDir, data, off, lim, total);
        else
            writeJson("occupancy", args.uiOutputDir, data, off, lim, total);
    }
    else
    {
        nlohmann::json data;
        data["events"] = std::move(events);

        if (args.compact)
            writeJsonCompact("occupancy", args.uiOutputDir, data);
        else
            writeJson("occupancy", args.uiOutputDir, data);
    }

    return 0;
}

} // namespace Headless
