// MIT License
//
// Copyright (c) 2024-2026 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "wavedata.h"
#include "util/jsonfilereader.h"

bool WaveData::Load(const std::string& filepath)
{
    path = filepath;

    JsonFileReader json(path);
    if (!json.bValid) return false;

    auto& data = json.data;
    if (!data.contains("wave")) return false;

    auto& wave_json = data["wave"];

    wave_id = wave_json.value("id", -1);
    wave_begin = int64_t(wave_json["begin"]);
    wave_end = int64_t(wave_json["end"]);

    if (wave_json.contains("cu"))
        cu = int(wave_json["cu"]);

    // Load code from sibling code.json
    std::string code_path = path.substr(0, path.rfind("se")) + "code.json";
    try { code = CodeData::LoadCode(code_path); }
    catch (...) {}

    // Parse instructions
    if (wave_json.contains("instructions"))
    {
        for (auto& inst : wave_json["instructions"])
        {
            WaveInstruction wi;
            wi.clock = int64_t(inst[0]);
            wi.type = int(inst[1]);
            wi.stall = int(inst[2]);
            wi.cycles = std::max(wi.stall, int(inst[3]));
            wi.code_line = int(inst[4]);
            instructions.push_back(wi);
            line_to_clock[wi.code_line].push_back(wi.clock);
        }
    }

    // Parse timeline
    if (wave_json.contains("timeline"))
    {
        int64_t clock = wave_begin;
        for (auto& time : wave_json["timeline"])
        {
            TimelineEntry entry;
            entry.clock = clock;
            entry.duration = int(time[1]);
            entry.state = int(time[0]);
            timeline.push_back(entry);
            clock += entry.duration;
        }
    }

    // Parse waitcnt
    if (wave_json.contains("waitcnt"))
    {
        for (auto& array : wave_json["waitcnt"])
        {
            WaitCntEntry entry;
            entry.code_line = int(array[0]);
            for (auto& pair : array[1])
                entry.sources.push_back({int(pair[0]), int(pair[1])});
            waitcnt.push_back(std::move(entry));
        }
    }

    // Parse info
    if (wave_json.contains("info"))
    {
        auto& info_json = wave_json["info"];
        std::vector<std::string> info_params;
        for (auto& [param, value] : info_json.items())
            if (param.find("_stall") == std::string::npos)
                info_params.push_back(param);

        for (auto& param : info_params)
        {
            int64_t stall_cnt = 0;
            try { stall_cnt = int64_t(info_json[param + "_stall"]); }
            catch (...) {}

            try { wave_info.push_back({param, int64_t(info_json[param]), stall_cnt}); }
            catch (...) {}
        }
    }

    for (auto& [_, clocks] : line_to_clock) clocks.shrink_to_fit();

    return true;
}
