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

    // Collect wave file paths from manifest
    struct WaveRef
    {
        std::string path;
        int se;
        int simd;
        int slot;
        int waveId;
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
                    if (info.size() < 1) continue;

                    std::string filename = info[0].get<std::string>();
                    std::string fullPath = cache.baseDir() + filename;

                    waveRefs.push_back({fullPath, se, simd, slot, waveId});
                }
            }
        }
    }

    // Apply CU filter after loading (CU comes from the wave file itself)
    nlohmann::json wavesArray = nlohmann::json::array();
    for (auto& ref : waveRefs)
    {
        try
        {
            auto& wave = cache.loadWave(ref.path);

            if (filterCU >= 0 && wave.cu != filterCU) continue;

            nlohmann::json waveJson;
            waveJson["se"] = ref.se;
            waveJson["simd"] = ref.simd;
            waveJson["slot"] = ref.slot;
            waveJson["wave_id"] = wave.wave_id;
            waveJson["cu"] = wave.cu;
            waveJson["begin"] = wave.wave_begin;
            waveJson["end"] = wave.wave_end;

            // Instructions
            nlohmann::json instsJson = nlohmann::json::array();
            for (auto& inst : wave.instructions)
            {
                nlohmann::json ij;
                ij["clock"] = inst.clock;
                ij["type"] = inst.type;
                ij["stall"] = inst.stall;
                ij["cycles"] = inst.cycles;
                ij["code_line"] = inst.code_line;
                instsJson.push_back(std::move(ij));
            }
            waveJson["instructions"] = std::move(instsJson);

            // Timeline
            nlohmann::json timelineJson = nlohmann::json::array();
            for (auto& entry : wave.timeline)
            {
                nlohmann::json tj;
                tj["clock"] = entry.clock;
                tj["duration"] = entry.duration;
                tj["state"] = entry.state;
                timelineJson.push_back(std::move(tj));
            }
            waveJson["timeline"] = std::move(timelineJson);

            // Info
            nlohmann::json infoJson = nlohmann::json::array();
            for (auto& ie : wave.wave_info)
            {
                nlohmann::json ij;
                ij["name"] = ie.name;
                ij["value"] = ie.value;
                ij["stalls"] = ie.stalls;
                infoJson.push_back(std::move(ij));
            }
            waveJson["info"] = std::move(infoJson);

            wavesArray.push_back(std::move(waveJson));
        }
        catch (const std::exception& e)
        {
            // Skip waves that fail to load
            continue;
        }
    }

    int total = static_cast<int>(wavesArray.size());

    // Apply pagination on the waves array
    if (args.offset > 0 || args.limit > 0)
    {
        int off = std::max(0, args.offset);
        int lim = args.limit > 0 ? args.limit : total;
        off = std::min(off, total);
        lim = std::min(lim, total - off);

        nlohmann::json paginated = nlohmann::json::array();
        for (int i = off; i < off + lim; ++i)
            paginated.push_back(wavesArray[i]);

        nlohmann::json data;
        data["waves"] = std::move(paginated);

        if (args.compact)
            writeJsonCompact("waves", args.uiOutputDir, data, off, lim, total);
        else
            writeJson("waves", args.uiOutputDir, data, off, lim, total);
    }
    else
    {
        nlohmann::json data;
        data["waves"] = std::move(wavesArray);

        if (args.compact)
            writeJsonCompact("waves", args.uiOutputDir, data);
        else
            writeJson("waves", args.uiOutputDir, data);
    }

    return 0;
}

} // namespace Headless
