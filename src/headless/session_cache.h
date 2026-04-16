// SPDX-License-Identifier: MIT
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>
#include "code/codeload.hpp"
#include "data/shaderdata.h"
#include "data/wavedata.h"
#include "json/include/nlohmann/json.hpp"

namespace Headless
{

/// Lazily-loaded session data cache for the CLI.
/// Parses each data source on first access and returns cached results afterward.
/// In interactive mode, a single SessionCache persists across commands.
class SessionCache
{
public:
    explicit SessionCache(const std::string& uiOutputDir);

    /// Get the base directory (ui_output_dir with trailing slash).
    const std::string& baseDir() const { return m_baseDir; }

    /// Load and return the manifest (filenames.json).
    const nlohmann::json& loadManifest();

    /// Load and return the ISA code array.
    const std::vector<CodeData>& loadCode();

    /// Load a single wave file and return parsed WaveData.
    /// Results are cached by filepath.
    const WaveData& loadWave(const std::string& filepath);

    /// Load and return occupancy data.
    const nlohmann::json& loadOccupancy();

    /// Load and return the shaderdata manager.
    ShaderDataManager& loadShaderData();

    /// Load and return realtime/event data.
    const nlohmann::json& loadRealtime();

    /// Get counter names from manifest.
    std::vector<std::string> counterNames();

    /// Get the number of shader engines from the manifest.
    int numShaderEngines();

    /// Build code map (code_index -> "source: instruction") for latency analysis.
    std::map<int, std::string> buildCodeMap();

private:
    std::string m_baseDir;

    bool m_manifestLoaded = false;
    nlohmann::json m_manifest;

    bool m_codeLoaded = false;
    std::vector<CodeData> m_code;

    std::map<std::string, WaveData> m_waves;

    bool m_occupancyLoaded = false;
    nlohmann::json m_occupancy;

    bool m_shaderDataLoaded = false;
    ShaderDataManager m_shaderData;

    bool m_realtimeLoaded = false;
    nlohmann::json m_realtime;
};

} // namespace Headless
