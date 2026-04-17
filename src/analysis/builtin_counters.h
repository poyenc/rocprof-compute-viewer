// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace BuiltinCounters
{

inline const std::vector<std::pair<std::string, int>>& utilTypes()
{
    static const std::vector<std::pair<std::string, int>> types = {
        {"MISC", 100}, {"FLAT", 200}, {"SCA", 100}, {"LDS", 200}, {"VMEM", 200}
    };
    return types;
}

inline const std::vector<std::string>& mopsTypes()
{
    static const std::vector<std::string> types = {
        "I8", "F8", "F16", "BF16", "F32", "F64", "XF32", "F6F4"
    };
    return types;
}

inline std::string getDefinitions()
{
    std::string derived = "_reduce_busy := sum[max[BUSY_CU_CYCLES, axis=TIME], axis=[XCC,SE,CU]] + 1E-6";

    for (auto& [name, mult] : utilTypes())
        derived += "\n" + name + "_util := " + std::to_string(mult) + " * sum[ACTIVE_INST_" + name +
                   ", axis=[XCC,SE,CU]] / _reduce_busy";

    derived += "\n_mfmabusy := sum[VALU_MFMA_BUSY_CYCLES, axis=[XCC,SE,CU]] / _reduce_busy / 4"
               "\n_valubusy := sum[ACTIVE_INST_VALU, axis=[XCC,SE,CU]] / _reduce_busy\n"
               "\nVALU_util := 100 * _valubusy"
               "\nMFMA_util := 100 * _mfmabusy\n"
               "\nGPUutil := max(LDS_util, VMEM_util, FLAT_util, min(MFMA_util + VALU_util * (1 - _mfmabusy) / (1.7 - "
               "_mfmabusy), 100))";

    derived += "\n_clock_delta := select[RCLOCK, -1, axis=TIME] - select[RCLOCK, 0, axis=TIME]";
    derived += "\n_frequency := 1E8 * select[_clock_delta, 0, axis=CU] / select[_clock_delta, 1, axis=CU]";
    derived += "\n_delta_seconds := min[delta[SCLOCK, axis=TIME]] / _frequency + 1E-13";

    for (const std::string& name : mopsTypes())
        derived += "\n" + name + "_TFLOPS := 512E-12 * sum[INSTS_VALU_MFMA_MOPS_" + name +
                   ", axis=[XCC,SE,CU]] / _delta_seconds";

    return derived;
}

inline std::vector<std::string> utilizationNames()
{
    std::vector<std::string> names;
    for (auto& [name, _] : utilTypes())
        names.push_back(name + "_util");
    names.push_back("VALU_util");
    names.push_back("MFMA_util");
    names.push_back("GPUutil");
    return names;
}

inline std::vector<std::string> tflopsNames()
{
    std::vector<std::string> names;
    for (auto& name : mopsTypes())
        names.push_back(name + "_TFLOPS");
    return names;
}

} // namespace BuiltinCounters
