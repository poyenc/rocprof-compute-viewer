// SPDX-License-Identifier: MIT
#include "session_cache.h"
#include "util/jsonfilereader.h"

namespace Headless
{

SessionCache::SessionCache(const std::string& uiOutputDir) : m_baseDir(uiOutputDir)
{
    // Ensure trailing slash
    if (!m_baseDir.empty() && m_baseDir.back() != '/' && m_baseDir.back() != '\\')
        m_baseDir += '/';
}

const nlohmann::json& SessionCache::loadManifest()
{
    if (m_manifestLoaded) return m_manifest;

    JsonFileReader reader(m_baseDir + "filenames.json");
    if (!reader.bValid)
        throw std::runtime_error("Failed to load manifest: " + m_baseDir + "filenames.json");

    m_manifest = std::move(reader.data);
    m_manifestLoaded = true;
    return m_manifest;
}

const std::vector<CodeData>& SessionCache::loadCode()
{
    if (m_codeLoaded) return m_code;

    m_code = CodeData::LoadCode(m_baseDir + "code.json");
    m_codeLoaded = true;
    return m_code;
}

const WaveData& SessionCache::loadWave(const std::string& filepath)
{
    auto it = m_waves.find(filepath);
    if (it != m_waves.end()) return it->second;

    WaveData wave;
    if (!wave.Load(filepath))
        throw std::runtime_error("Failed to load wave file: " + filepath);

    auto result = m_waves.emplace(filepath, std::move(wave));
    return result.first->second;
}

const nlohmann::json& SessionCache::loadOccupancy()
{
    if (m_occupancyLoaded) return m_occupancy;

    JsonFileReader reader(m_baseDir + "occupancy.json", false);
    if (reader.bValid)
        m_occupancy = std::move(reader.data);
    else
        m_occupancy = nlohmann::json();
    m_occupancyLoaded = true;
    return m_occupancy;
}

ShaderDataManager& SessionCache::loadShaderData()
{
    if (m_shaderDataLoaded) return m_shaderData;

    auto& manifest = loadManifest();
    if (manifest.contains("shaderdata_filenames"))
        m_shaderData.Load(manifest["shaderdata_filenames"], m_baseDir);

    m_shaderDataLoaded = true;
    return m_shaderData;
}

const nlohmann::json& SessionCache::loadRealtime()
{
    if (m_realtimeLoaded) return m_realtime;

    JsonFileReader reader(m_baseDir + "realtime.json", false);
    if (reader.bValid)
        m_realtime = std::move(reader.data);
    else
        m_realtime = nlohmann::json::object();

    m_realtimeLoaded = true;
    return m_realtime;
}

std::vector<std::string> SessionCache::counterNames()
{
    auto& manifest = loadManifest();
    std::vector<std::string> names;
    if (manifest.contains("counter_names"))
        for (auto& name : manifest["counter_names"])
            names.push_back(name.get<std::string>());
    return names;
}

int SessionCache::numShaderEngines()
{
    auto& manifest = loadManifest();
    if (manifest.contains("num_se"))
        return manifest["num_se"].get<int>();
    // Fallback: count wave_filenames keys
    if (manifest.contains("wave_filenames"))
        return static_cast<int>(manifest["wave_filenames"].size());
    return 0;
}

std::map<int, std::string> SessionCache::buildCodeMap()
{
    auto& code = loadCode();
    std::map<int, std::string> codeMap;
    for (auto& c : code)
    {
        std::string source = c.line->cppline;
        auto pos = source.rfind('/');
        if (pos != std::string::npos) source = source.substr(pos + 1);

        codeMap[c.line->index] = source + ": " + c.line->inst;
    }
    return codeMap;
}

} // namespace Headless
