// SPDX-License-Identifier: MIT
#include "cmd_waves.h"
#include "json_output.h"

namespace Headless
{

int cmdWaves(const HeadlessArgs& args, SessionCache& cache)
{
    auto& manifest = cache.loadManifest();

    if (!manifest.contains("wave_filenames"))
    {
        writeError("No wave data in this session (no wave_filenames in manifest)");
        return 1;
    }

    // Parse filters
    int filterSE = -1;
    int filterCU = -1;
    int filterSIMD = -1;
    std::string seFilterStr = getOption(args.options, "--se");
    std::string cuFilterStr = getOption(args.options, "--cu");
    std::string simdFilterStr = getOption(args.options, "--simd");
    if (!seFilterStr.empty()) filterSE = std::stoi(seFilterStr);
    if (!cuFilterStr.empty()) filterCU = std::stoi(cuFilterStr);
    if (!simdFilterStr.empty()) filterSIMD = std::stoi(simdFilterStr);

    // Collect wave references from manifest (without loading wave files)
    struct WaveRef
    {
        std::string filename;
        std::string fullPath;
        int se;
        int simd;
        int slot;
        int waveId;
        int64_t begin;  // from manifest
        int64_t end;    // from manifest
    };
    std::vector<WaveRef> waveRefs;

    for (auto& [seKey, simds] : manifest["wave_filenames"].items())
    {
        int se = std::stoi(seKey);
        if (filterSE >= 0 && se != filterSE) continue;

        for (auto& [simdKey, slots] : simds.items())
        {
            int simd = std::stoi(simdKey);
            if (filterSIMD >= 0 && simd != filterSIMD) continue;

            for (auto& [slotStr, waves] : slots.items())
            {
                int slot = std::stoi(slotStr);

                for (auto& [waveIdStr, info] : waves.items())
                {
                    int waveId = std::stoi(waveIdStr);
                    if (info.size() < 3) continue;

                    std::string filename = info[0].get<std::string>();
                    int64_t beginClock = info[1].get<int64_t>();
                    int64_t endClock = info[2].get<int64_t>();

                    waveRefs.push_back({
                        filename,
                        cache.baseDir() + filename,
                        se, simd, slot, waveId,
                        beginClock, endClock
                    });
                }
            }
        }
    }

    // Note: CU filter requires loading wave files (CU is inside the wave data).
    // For CU-filtered queries, we must load waves to check.
    // For unfiltered queries, we can use manifest data for the summary
    // and only load wave files within the pagination window.

    int total = static_cast<int>(waveRefs.size());
    int off = std::max(0, args.offset);
    int lim = args.limit > 0 ? args.limit : total;
    off = std::min(off, total);
    lim = std::min(lim, total - off);

    nlohmann::json wavesArray = nlohmann::json::array();

    if (filterCU >= 0)
    {
        // CU filter: must load all waves to check CU, then paginate
        std::vector<int> matchingIndices;
        for (int i = 0; i < total; ++i)
        {
            try
            {
                auto& wave = cache.loadWave(waveRefs[i].fullPath);
                if (wave.cu == filterCU)
                    matchingIndices.push_back(i);
            }
            catch (...) {}
        }

        total = static_cast<int>(matchingIndices.size());
        off = std::min(off, total);
        lim = std::min(lim, total - off);

        for (int j = off; j < off + lim; ++j)
        {
            auto& ref = waveRefs[matchingIndices[j]];
            auto& wave = cache.loadWave(ref.fullPath);

            nlohmann::json wj;
            wj["se"] = ref.se;
            wj["simd"] = ref.simd;
            wj["slot"] = ref.slot;
            wj["wave_id"] = wave.wave_id;
            wj["cu"] = wave.cu;
            wj["begin"] = wave.wave_begin;
            wj["end"] = wave.wave_end;
            wj["instruction_count"] = static_cast<int>(wave.instructions.size());

            nlohmann::json instsJson = nlohmann::json::array();
            for (auto& inst : wave.instructions)
                instsJson.push_back({{"clock", inst.clock}, {"type", inst.type},
                                     {"stall", inst.stall}, {"cycles", inst.cycles},
                                     {"code_line", inst.code_line}});
            wj["instructions"] = std::move(instsJson);

            nlohmann::json timelineJson = nlohmann::json::array();
            for (auto& e : wave.timeline)
                timelineJson.push_back({{"clock", e.clock}, {"duration", e.duration}, {"state", e.state}});
            wj["timeline"] = std::move(timelineJson);

            nlohmann::json infoJson = nlohmann::json::array();
            for (auto& ie : wave.wave_info)
                infoJson.push_back({{"name", ie.name}, {"value", ie.value}, {"stalls", ie.stalls}});
            wj["info"] = std::move(infoJson);

            wavesArray.push_back(std::move(wj));
        }
    }
    else
    {
        // No CU filter: only load wave files within the pagination window
        for (int i = off; i < off + lim; ++i)
        {
            auto& ref = waveRefs[i];
            try
            {
                auto& wave = cache.loadWave(ref.fullPath);

                nlohmann::json wj;
                wj["se"] = ref.se;
                wj["simd"] = ref.simd;
                wj["slot"] = ref.slot;
                wj["wave_id"] = wave.wave_id;
                wj["cu"] = wave.cu;
                wj["begin"] = wave.wave_begin;
                wj["end"] = wave.wave_end;
                wj["instruction_count"] = static_cast<int>(wave.instructions.size());

                nlohmann::json instsJson = nlohmann::json::array();
                for (auto& inst : wave.instructions)
                    instsJson.push_back({{"clock", inst.clock}, {"type", inst.type},
                                         {"stall", inst.stall}, {"cycles", inst.cycles},
                                         {"code_line", inst.code_line}});
                wj["instructions"] = std::move(instsJson);

                nlohmann::json timelineJson = nlohmann::json::array();
                for (auto& e : wave.timeline)
                    timelineJson.push_back({{"clock", e.clock}, {"duration", e.duration}, {"state", e.state}});
                wj["timeline"] = std::move(timelineJson);

                nlohmann::json infoJson = nlohmann::json::array();
                for (auto& ie : wave.wave_info)
                    infoJson.push_back({{"name", ie.name}, {"value", ie.value}, {"stalls", ie.stalls}});
                wj["info"] = std::move(infoJson);

                wavesArray.push_back(std::move(wj));
            }
            catch (...)
            {
                continue;
            }
        }
    }

    nlohmann::json data;
    data["waves"] = std::move(wavesArray);

    if (args.offset > 0 || args.limit > 0)
    {
        if (args.compact)
            writeJsonCompact("waves", args.uiOutputDir, data, off, lim, total);
        else
            writeJson("waves", args.uiOutputDir, data, off, lim, total);
    }
    else
    {
        if (args.compact)
            writeJsonCompact("waves", args.uiOutputDir, data);
        else
            writeJson("waves", args.uiOutputDir, data);
    }

    return 0;
}

} // namespace Headless
