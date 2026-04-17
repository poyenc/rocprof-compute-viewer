// SPDX-License-Identifier: MIT
#include "cmd_info.h"
#include "json_output.h"

namespace Headless
{

int cmdInfo(const HeadlessArgs& args, SessionCache& cache)
{
    auto& manifest = cache.loadManifest();

    nlohmann::json data;

    // Basic metadata
    if (manifest.contains("gfxip")) data["gfxip"] = manifest["gfxip"];
    if (manifest.contains("gfxv")) data["gfxv"] = manifest["gfxv"];
    if (manifest.contains("version")) data["version"] = manifest["version"];
    if (manifest.contains("pc_sampling")) data["pc_sampling"] = manifest["pc_sampling"];
    if (manifest.contains("thread_trace")) data["thread_trace"] = manifest["thread_trace"];
    if (manifest.contains("num_se")) data["num_se"] = manifest["num_se"];

    // Counter names
    if (manifest.contains("counter_names"))
        data["counter_names"] = manifest["counter_names"];

    // Count SEs and waves from wave_filenames
    int numSe = 0;
    int numWaves = 0;
    if (manifest.contains("wave_filenames") && !manifest["wave_filenames"].is_null())
    {
        auto& wf = manifest["wave_filenames"];
        numSe = static_cast<int>(wf.size());
        for (auto& [se, simds] : wf.items())
            for (auto& [simd, slots] : simds.items())
                for (auto& [slot, waves] : slots.items())
                    numWaves += static_cast<int>(waves.size());
    }
    data["num_se"] = numSe;
    data["num_waves"] = numWaves;

    // Check for occupancy (occupancy.json is a separate file, check filesystem)
    data["has_occupancy"] = !cache.loadOccupancy().is_null();

    // Check for shaderdata
    data["has_shaderdata"] = manifest.contains("shaderdata_filenames");

    // Check for perfcounters
    data["has_perfcounters"] = manifest.contains("counter_names") &&
                               !manifest["counter_names"].empty();

    if (args.compact)
        writeJsonCompact("info", args.uiOutputDir, data);
    else
        writeJson("info", args.uiOutputDir, data);

    return 0;
}

} // namespace Headless
