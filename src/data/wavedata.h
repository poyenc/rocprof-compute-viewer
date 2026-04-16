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

#pragma once

#include "code/codeload.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct occupancy_data
{
    int64_t time{0};
    uint8_t cu{0};
    int8_t simd{0};
    int8_t slot{0};
    int8_t enable{0};
    int kernel_id{0};

    occupancy_data() = default;

    template <typename T> static occupancy_data build(T& v)
    {
        occupancy_data occ;
        occ.time = (int64_t) v[0];
        occ.cu = (uint8_t) v[1];
        occ.simd = (int8_t) v[2];
        occ.slot = (int8_t) v[3];
        occ.enable = (int8_t) v[4];
        occ.kernel_id = (int) v[5];
        return occ;
    };
};

struct WaveInstruction
{
    int64_t clock{0};
    int type{0};
    int stall{0};
    int cycles{0};
    int code_line{0};
};

struct WaitCntEntry
{
    int code_line{0};
    std::vector<std::pair<int, int>> sources;
};

struct WaveInfoEntry
{
    std::string name;
    int64_t value{0};
    int64_t stalls{0};
};

struct TimelineEntry
{
    int64_t clock{0};
    int duration{0};
    int state{0};
};

struct WaveData
{
    int64_t wave_begin{0};
    int64_t wave_end{0};
    int cu{-1};
    int wave_id{-1};
    std::string path;

    std::vector<CodeData> code;
    std::vector<WaveInstruction> instructions;
    std::vector<TimelineEntry> timeline;
    std::vector<WaitCntEntry> waitcnt;
    std::vector<WaveInfoEntry> wave_info;
    std::map<int, std::vector<int64_t>> line_to_clock;

    bool Load(const std::string& filepath);
};
