// SPDX-License-Identifier: MIT
#include "cmd_occupancy.h"
#include "json_output.h"

namespace Headless
{

int cmdOccupancy(const HeadlessArgs& args, SessionCache& cache)
{
    auto& occupancy = cache.loadOccupancy();

    if (occupancy.is_null() || occupancy.empty())
    {
        nlohmann::json data;
        data["events"] = nlohmann::json::array();
        if (args.compact)
            writeJsonCompact("occupancy", args.uiOutputDir, data);
        else
            writeJson("occupancy", args.uiOutputDir, data);
        return 0;
    }

    // Parse filters
    int filterSE = -1;
    int filterCU = -1;
    std::string seStr = getOption(args.options, "--se");
    std::string cuStr = getOption(args.options, "--cu");
    if (!seStr.empty()) filterSE = std::stoi(seStr);
    if (!cuStr.empty()) filterCU = std::stoi(cuStr);

    nlohmann::json events = nlohmann::json::array();

    // Include dispatch names if available
    nlohmann::json dispatches;
    if (occupancy.contains("dispatches"))
        dispatches = occupancy["dispatches"];

    // occupancy.json format: top-level keys are SE numbers ("0", "1", ...)
    // plus "version" and "dispatches". Each SE has an array of events:
    // [timestamp, cu, simd, slot, enable, kernel_id]
    for (auto& [key, value] : occupancy.items())
    {
        if (key == "version" || key == "dispatches") continue;
        if (!value.is_array()) continue;

        int se = -1;
        try { se = std::stoi(key); }
        catch (...) { continue; }

        if (filterSE >= 0 && se != filterSE) continue;

        for (auto& entry : value)
        {
            if (entry.size() < 6) continue;

            int cu = static_cast<int>(entry[1]);
            if (filterCU >= 0 && cu != filterCU) continue;

            nlohmann::json ev;
            ev["se"] = se;
            ev["timestamp"] = entry[0];
            ev["cu"] = cu;
            ev["simd"] = static_cast<int>(entry[2]);
            ev["slot"] = static_cast<int>(entry[3]);
            ev["enable"] = static_cast<int>(entry[4]);
            ev["kernel_id"] = static_cast<int>(entry[5]);
            events.push_back(std::move(ev));
        }
    }

    int total = static_cast<int>(events.size());

    // Apply pagination
    int off = std::max(0, args.offset);
    int lim = args.limit > 0 ? args.limit : total;
    off = std::min(off, total);
    lim = std::min(lim, total - off);

    nlohmann::json paginated = nlohmann::json::array();
    for (int i = off; i < off + lim; ++i)
        paginated.push_back(events[i]);

    nlohmann::json data;
    if (!dispatches.is_null())
        data["dispatches"] = dispatches;
    data["events"] = std::move(paginated);

    if (args.offset > 0 || args.limit > 0)
    {
        if (args.compact)
            writeJsonCompact("occupancy", args.uiOutputDir, data, off, lim, total);
        else
            writeJson("occupancy", args.uiOutputDir, data, off, lim, total);
    }
    else
    {
        if (args.compact)
            writeJsonCompact("occupancy", args.uiOutputDir, data);
        else
            writeJson("occupancy", args.uiOutputDir, data);
    }

    return 0;
}

} // namespace Headless
