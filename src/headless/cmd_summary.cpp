// SPDX-License-Identifier: MIT
#include "cmd_summary.h"
#include "analysis/builtin_counters.h"
#include "analysis/derived_counter.h"
#include "counter_tensors.h"
#include "json_output.h"

#include <algorithm>

namespace Headless
{

int cmdSummary(const HeadlessArgs& args, SessionCache& cache)
{
    int topN = 10;
    std::string topStr = getOption(args.options, "--top");
    if (!topStr.empty()) topN = std::stoi(topStr);

    nlohmann::json data;

    // --- Session metadata ---
    auto& manifest = cache.loadManifest();
    nlohmann::json session;
    if (manifest.contains("gfxip")) session["gfxip"] = manifest["gfxip"];
    if (manifest.contains("gfxv")) session["gfxv"] = manifest["gfxv"];
    session["num_se"] = cache.numShaderEngines();

    int numWaves = 0;
    if (manifest.contains("wave_filenames") && !manifest["wave_filenames"].is_null())
        for (auto& [se, simds] : manifest["wave_filenames"].items())
            for (auto& [simd, slots] : simds.items())
                for (auto& [slot, waves] : slots.items())
                    numWaves += static_cast<int>(waves.size());
    session["num_waves"] = numWaves;
    data["session"] = session;

    // --- Activity distribution (from ISA code data) ---
    auto& code = cache.loadCode();

    int64_t idle = 0, stall = 0, exec = 0;
    for (auto& c : code)
    {
        if (!c.line) continue;
        idle += c.line->idle_sum;
        stall += c.line->stall_sum;
        exec += c.line->latency_sum - c.line->stall_sum;
    }

    int64_t total = idle + stall + exec;
    nlohmann::json activity;
    activity["idle_cycles"] = idle;
    activity["stall_cycles"] = stall;
    activity["exec_cycles"] = exec;

    if (total > 0)
    {
        activity["idle_pct"] = 100.0 * idle / total;
        activity["stall_pct"] = 100.0 * stall / total;
        activity["exec_pct"] = 100.0 * exec / total;
    }
    else
    {
        activity["idle_pct"] = 0.0;
        activity["stall_pct"] = 0.0;
        activity["exec_pct"] = 0.0;
    }
    data["activity"] = activity;

    // --- Hotspots (top instructions by cycles) ---
    struct HotspotEntry
    {
        int index;
        std::string opcode;
        std::string source;
        int64_t cycles;
    };

    std::vector<HotspotEntry> entries;
    int64_t totalCycles = 0;
    for (auto& c : code)
    {
        if (!c.line) continue;
        totalCycles += c.line->latency_sum;
        if (c.line->latency_sum > 0)
            entries.push_back({c.line->index.load(), c.line->inst, c.line->cppline, c.line->latency_sum});
    }

    std::sort(entries.begin(), entries.end(),
        [](const HotspotEntry& a, const HotspotEntry& b) { return a.cycles > b.cycles; });

    if (topN > 0 && topN < static_cast<int>(entries.size()))
        entries.resize(topN);

    nlohmann::json hotspots = nlohmann::json::array();
    int rank = 1;
    for (auto& e : entries)
    {
        nlohmann::json h;
        h["rank"] = rank++;
        h["code_index"] = e.index;
        h["opcode"] = e.opcode;
        h["source"] = e.source;
        h["cycles"] = e.cycles;
        h["pct"] = totalCycles > 0 ? 100.0 * e.cycles / totalCycles : 0.0;
        hotspots.push_back(std::move(h));
    }
    data["hotspots"] = hotspots;

    // --- Utilization and throughput (from derived counters) ---
    auto counterNames = cache.counterNames();
    if (!counterNames.empty())
    {
        DerivedCounter::DerivedCounterManager mgr;
        buildCounterTensors(cache, mgr);

        try
        {
            mgr.loadDefinitions(BuiltinCounters::getDefinitions());

            // Extract utilization scalars
            nlohmann::json utilization;
            for (auto& name : BuiltinCounters::utilizationNames())
            {
                try
                {
                    auto result = mgr.evaluate(name);
                    if (result && result->isScalar())
                    {
                        std::string key = name;
                        if (key.size() > 5 && key.substr(key.size() - 5) == "_util")
                            key = key.substr(0, key.size() - 5);
                        else if (key == "GPUutil")
                            key = "GPU";
                        utilization[key] = result->scalar();
                    }
                }
                catch (...) {}
            }
            if (!utilization.empty())
                data["utilization"] = utilization;

            // Extract throughput scalars
            nlohmann::json throughput;
            for (auto& name : BuiltinCounters::tflopsNames())
            {
                try
                {
                    auto result = mgr.evaluate(name);
                    if (result && result->isScalar())
                        throughput[name] = result->scalar();
                }
                catch (...) {}
            }
            if (!throughput.empty())
                data["throughput"] = throughput;
        }
        catch (const std::exception&)
        {
            data["note"] = "Builtin counter evaluation failed. Required counters may be missing.";
        }
    }
    else
    {
        data["note"] = "No performance counters in this trace. Utilization and throughput unavailable.";
    }

    if (args.compact)
        writeJsonCompact("summary", args.uiOutputDir, data);
    else
        writeJson("summary", args.uiOutputDir, data);

    return 0;
}

} // namespace Headless
