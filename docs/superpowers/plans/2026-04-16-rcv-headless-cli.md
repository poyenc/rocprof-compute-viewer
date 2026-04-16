# RCV Headless CLI — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a separate `rocprof-compute-viewer-cli` binary that outputs GPU thread trace analysis as JSON, sharing a refactored Qt-free core with the GUI binary.

**Architecture:** Refactor three Qt boundaries (JsonRequest, CustomTokens, WaveInstance) to extract Qt-free bases, then build a CLI binary in `src/headless/` that links only Qt-free sources. The CLI supports both single-command and interactive (stdin/stdout) modes with in-memory caching.

**Tech Stack:** C++20, nlohmann/json (vendored at `src/json/`), CMake, pthreads

**Spec:** `docs/superpowers/specs/2026-04-15-rcv-headless-cli-design.md`

---

## File Structure

```
src/
├── util/
│   ├── jsonfilereader.h          # CREATE — Qt-free JSON file loader (R1)
│   ├── jsonfilereader.cpp        # CREATE
│   ├── jsonrequest.hpp           # MODIFY — inherit from JsonFileReader
│   ├── jsonrequest.cpp           # MODIFY — delegate local file reads
│   └── tokentypemap.h            # CREATE — Qt-free instruction-to-type mapping (R2)
├── data/
│   ├── wavedata.h                # MODIFY — already exists as occupancy_data header; add WaveData class (R3)
│   ├── wavedata.cpp              # CREATE — wave data loading extracted from WaveInstance
│   ├── wavemanager.h             # MODIFY — WaveInstance inherits WaveData
│   └── wavemanager.cpp           # MODIFY — constructor delegates to WaveData::Load
│   ├── shaderdata.cpp            # MODIFY — use JsonFileReader instead of JsonRequest
├── code/
│   └── codeload.cpp              # MODIFY — use JsonFileReader + TokenTypeMap
├── headless/
│   ├── CMakeLists.txt            # CREATE — CLI target
│   ├── main.cpp                  # CREATE — CLI entry point
│   ├── headless_dispatcher.h     # CREATE — command routing + arg parsing
│   ├── headless_dispatcher.cpp   # CREATE
│   ├── json_output.h             # CREATE — envelope, error, pagination helpers
│   ├── session_cache.h           # CREATE — in-memory parsed data cache
│   ├── session_cache.cpp         # CREATE
│   ├── cmd_info.h                # CREATE
│   ├── cmd_info.cpp              # CREATE
│   ├── cmd_isa.h                 # CREATE
│   ├── cmd_isa.cpp               # CREATE
│   ├── cmd_waves.h               # CREATE
│   ├── cmd_waves.cpp             # CREATE
│   ├── cmd_occupancy.h           # CREATE
│   ├── cmd_occupancy.cpp         # CREATE
│   ├── cmd_latency.h             # CREATE
│   ├── cmd_latency.cpp           # CREATE
│   ├── cmd_counters.h            # CREATE
│   ├── cmd_counters.cpp          # CREATE
│   ├── cmd_perfcounters.h        # CREATE
│   └── cmd_perfcounters.cpp      # CREATE
CMakeLists.txt                    # MODIFY — add_subdirectory(src/headless)
tests/
├── headless/
│   ├── CMakeLists.txt            # CREATE
│   └── test_headless_cli.sh      # CREATE — integration tests
SKILL.md                          # CREATE — agent discovery
```

---

## Phase 1: Refactoring — Qt-Free Core Extraction

### Task 1: Create JsonFileReader (R1)

**Files:**
- Create: `src/util/jsonfilereader.h`
- Create: `src/util/jsonfilereader.cpp`

- [ ] **Step 1: Create the JsonFileReader header**

```cpp
// src/util/jsonfilereader.h
#pragma once

#include <fstream>
#include <iostream>
#include <string>
#include "json/include/nlohmann/json.hpp"

/// Qt-free JSON file loader. Reads a local JSON file via std::ifstream.
/// This is the shared base for both CLI and GUI JSON loading.
/// The GUI's JsonRequest extends this with QNetwork HTTP support.
class JsonFileReader
{
public:
    explicit JsonFileReader(const std::string& path, bool bWarn = true);
    virtual ~JsonFileReader() = default;

    bool bValid = false;
    nlohmann::json data;

protected:
    JsonFileReader() = default;  // for derived classes that do their own loading
};
```

- [ ] **Step 2: Create the JsonFileReader implementation**

```cpp
// src/util/jsonfilereader.cpp
#include "jsonfilereader.h"

#ifndef QWARNING
#    define QWARNING(cond, msg, action) \
        if (!(cond))                    \
        {                               \
            std::cout << msg << "\n";   \
            action;                     \
        }
#endif

JsonFileReader::JsonFileReader(const std::string& path, bool bWarn)
{
    std::ifstream f(path);
    if (!f.good())
    {
        QWARNING(!bWarn, "Could not parse: " << path, return);
        return;
    }
    try
    {
        data = nlohmann::json::parse(f);
        bValid = true;
    }
    catch (const nlohmann::json::parse_error& e)
    {
        QWARNING(!bWarn, "JSON parse error in " << path << ": " << e.what(), return);
    }
}
```

- [ ] **Step 3: Verify it compiles standalone**

```bash
cd build
g++ -std=c++20 -c -I../src -I../src/json/include ../src/util/jsonfilereader.cpp -o /dev/null
```

Expected: compiles with no errors.

- [ ] **Step 4: Commit**

```bash
git add src/util/jsonfilereader.h src/util/jsonfilereader.cpp
git commit -m "feat: add JsonFileReader — Qt-free JSON file loader"
```

---

### Task 2: Create TokenTypeMap (R2)

**Files:**
- Create: `src/util/tokentypemap.h`

- [ ] **Step 1: Create the TokenTypeMap header**

This extracts the instruction-prefix-to-type-index mapping from `Config::CustomTokens()` in `src/config/config.cpp:409-448`. The type indices must match the `TokenColors()` order in the GUI. The key names ("MATRIX") and their positions are stable.

```cpp
// src/util/tokentypemap.h
#pragma once

#include <fstream>
#include <string>
#include <utility>
#include <vector>
#include "json/include/nlohmann/json.hpp"

/// Qt-free instruction-prefix-to-type-index mapping.
/// Extracted from Config::CustomTokens() so that codeload.cpp
/// can classify instructions without depending on Qt/QColor.
///
/// The type index corresponds to the position in Config::TokenColors().
/// The default token color list (from config.cpp) is:
///   0=IDLE, 1=SCALAR, 2=VECTOR, 3=VMEM, 4=LDS, 5=EXPORT, 6=MATRIX,
///   7=BARRIER, 8=BRANCH/JUMP, 9=OTHER, 10=SPECIAL, 11=SMEM, 12=FLAT
/// These indices are stable; adding new types appends to the end.
namespace TokenTypeMap
{

/// Default type index for MATRIX instructions.
constexpr int MATRIX_TYPE_INDEX = 6;

/// Returns the default instruction-prefix-to-type-index mapping.
/// Optionally loads overrides from a `token_def.json` file (same format
/// as the GUI uses), but maps names to indices using the hardcoded list.
inline std::vector<std::pair<std::string, int>> defaults()
{
    // Hardcoded name-to-index mapping matching TokenColors() order
    static const std::vector<std::pair<std::string, int>> name_to_index = {
        {"IDLE", 0},    {"SCALAR", 1}, {"VECTOR", 2},  {"VMEM", 3},
        {"LDS", 4},     {"EXPORT", 5}, {"MATRIX", 6},  {"BARRIER", 7},
        {"JUMP", 8},    {"OTHER", 9},  {"SPECIAL", 10}, {"SMEM", 11},
        {"FLAT", 12},
    };

    auto find_index = [&](const std::string& name) -> int
    {
        for (auto& [n, i] : name_to_index)
            if (n == name) return i;
        return -1;
    };

    // Default prefix-to-name mapping
    nlohmann::json key;
    key["v_mfma"] = "MATRIX";
    key["v_smfma"] = "MATRIX";
    key["v_wmma"] = "MATRIX";
    key["v_swmma"] = "MATRIX";

    // Try loading overrides from token_def.json (same as GUI)
    try
    {
        std::ifstream ifs("token_def.json");
        if (ifs.is_open())
        {
            nlohmann::json new_asm;
            ifs >> new_asm;
            if (new_asm.contains("asmkeys"))
                for (auto& [isa, value] : new_asm["asmkeys"].items())
                    key[isa] = value;
        }
    }
    catch (...)
    {}

    std::vector<std::pair<std::string, int>> result;
    for (auto& [match, inst] : key.items())
    {
        int idx = find_index(inst.get<std::string>());
        if (idx >= 0) result.push_back({match, idx});
    }
    return result;
}

} // namespace TokenTypeMap
```

- [ ] **Step 2: Commit**

```bash
git add src/util/tokentypemap.h
git commit -m "feat: add TokenTypeMap — Qt-free instruction type classification"
```

---

### Task 3: Refactor codeload.cpp to use JsonFileReader + TokenTypeMap

**Files:**
- Modify: `src/code/codeload.cpp`

- [ ] **Step 1: Replace includes and usage**

In `src/code/codeload.cpp`, replace:

```cpp
#include "config/config.hpp"
#include "util/jsonrequest.hpp"
```

with:

```cpp
#include "util/jsonfilereader.h"
#include "util/tokentypemap.h"
```

- [ ] **Step 2: Replace JsonRequest with JsonFileReader**

Replace:

```cpp
    JsonRequest coderequest(path);

    if (coderequest.fail() || coderequest.bad()) throw std::exception{};
```

with:

```cpp
    JsonFileReader coderequest(path);

    if (!coderequest.bValid) throw std::exception{};
```

- [ ] **Step 3: Replace Config::CustomTokens with TokenTypeMap::defaults**

Replace:

```cpp
        for (auto& [custom_token, custom_type] : Config::CustomTokens())
            if (cache.back().line->inst.find(custom_token) == 0) cache.back().line->custom_type = custom_type;
```

with:

```cpp
        for (auto& [custom_token, custom_type] : TokenTypeMap::defaults())
            if (cache.back().line->inst.find(custom_token) == 0) cache.back().line->custom_type = custom_type;
```

- [ ] **Step 4: Build the GUI to verify no regressions**

```bash
cd build
cmake .. -DQT_VERSION_MAJOR=6
make -j$(nproc)
```

Expected: compiles successfully. The GUI binary still works.

- [ ] **Step 5: Commit**

```bash
git add src/code/codeload.cpp
git commit -m "refactor: decouple codeload.cpp from Qt (use JsonFileReader + TokenTypeMap)"
```

---

### Task 4: Refactor shaderdata.cpp to use JsonFileReader

**Files:**
- Modify: `src/data/shaderdata.cpp`

- [ ] **Step 1: Replace include**

In `src/data/shaderdata.cpp`, replace:

```cpp
#include "util/jsonrequest.hpp"
```

with:

```cpp
#include "util/jsonfilereader.h"
```

- [ ] **Step 2: Replace JsonRequest with JsonFileReader in LoadFile**

Replace:

```cpp
        JsonRequest request(filepath, false);
        if (!request.bValid) return local;

        auto& data = request.data;
```

with:

```cpp
        JsonFileReader request(filepath, false);
        if (!request.bValid) return local;

        auto& data = request.data;
```

- [ ] **Step 3: Build the GUI to verify no regressions**

```bash
cd build
make -j$(nproc)
```

Expected: compiles successfully.

- [ ] **Step 4: Commit**

```bash
git add src/data/shaderdata.cpp
git commit -m "refactor: decouple shaderdata.cpp from Qt (use JsonFileReader)"
```

---

### Task 5: Extract WaveData from WaveInstance (R3)

**Files:**
- Modify: `src/data/wavedata.h` (this file already exists — it contains `occupancy_data`)
- Create: `src/data/wavedata.cpp`
- Modify: `src/data/wavemanager.h`
- Modify: `src/data/wavemanager.cpp`

Note: `src/data/wavedata.h` already exists and contains `struct occupancy_data`. We add `WaveData` to it.

- [ ] **Step 1: Read the existing wavedata.h to understand current contents**

```bash
cat src/data/wavedata.h
```

- [ ] **Step 2: Add WaveData struct to wavedata.h**

Add the following after the existing `occupancy_data` struct in `src/data/wavedata.h`:

```cpp
#include "code/codeload.hpp"
#include <map>

/// Instruction execution record from a wave JSON file (Qt-free).
struct WaveInstruction
{
    int64_t clock{0};
    int type{0};
    int stall{0};
    int cycles{0};
    int code_line{0};
};

/// Wait count dependency entry (Qt-free).
/// Mirrors Canvas::WaitList but without the QWidget dependency.
struct WaitCntEntry
{
    int code_line{0};
    std::vector<std::pair<int, int>> sources; // (source_code_line, wait_type)
};

/// Aggregate info entry from wave["info"] (Qt-free).
struct WaveInfoEntry
{
    std::string name;
    int64_t value{0};
    int64_t stalls{0};
};

/// Timeline state entry (Qt-free).
struct TimelineEntry
{
    int64_t clock{0};
    int duration{0};
    int state{0}; // 0=Empty, 1=Idle, 2=Exec, 3=Wait, 4=Stall
};

/// Qt-free wave data loaded from a wave JSON file.
/// This is the shared base class; WaveInstance (GUI) extends it with
/// Token/TokenGroup rendering infrastructure.
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

    /// Load wave data from a wave JSON file. Returns false on failure.
    bool Load(const std::string& filepath);
};
```

- [ ] **Step 3: Create wavedata.cpp with the Load implementation**

Extract the data-loading logic from `WaveInstance::WaveInstance()` (wavemanager.cpp:219-365). Strip all Qt/Token/TokenGroup/mipmap code — keep only JSON parsing and data population.

```cpp
// src/data/wavedata.cpp
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
    try
    {
        code = CodeData::LoadCode(code_path);
    }
    catch (...)
    {
        // code.json might not exist for all wave files
    }

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

            try
            {
                wave_info.push_back({param, int64_t(info_json[param]), stall_cnt});
            }
            catch (...) {}
        }
    }

    // Shrink vectors
    for (auto& [_, clocks] : line_to_clock) clocks.shrink_to_fit();

    return true;
}
```

- [ ] **Step 4: Modify WaveInstance to inherit from WaveData**

In `src/data/wavemanager.h`, change:

```cpp
struct WaveInstance : public TokenGroup
```

to:

```cpp
struct WaveInstance : public WaveData, public TokenGroup
```

Remove the fields that are now in WaveData from WaveInstance:

```cpp
    // REMOVE these lines (now in WaveData):
    // std::vector<CodeData> code;
    // std::vector<Canvas::WaitList> waitcnt;
    // std::vector<WaveInfo> wave_info;
    // std::string path;
    // int cu = -1;
    // std::map<int, std::vector<int64_t>> line_to_clock{};
```

Keep `WaveInfo` struct definition in wavemanager.h (it's used by GUI code that doesn't know about `WaveInfoEntry`), but add a note that it mirrors `WaveInfoEntry`.

- [ ] **Step 5: Modify WaveInstance constructor to delegate to WaveData::Load**

In `src/data/wavemanager.cpp`, rewrite the constructor to:
1. Call `WaveData::Load(path)` for JSON parsing
2. Build Token objects from `this->instructions` (instead of from JSON directly)
3. Build timeline map from `this->timeline` entries
4. Build waitcnt from `this->waitcnt` entries (converting to `Canvas::WaitList`)
5. Keep the mipmap/rendering code

This is the most complex step. The key change is that the constructor no longer does JSON parsing — it gets data from WaveData fields and converts to Token/rendering structures.

- [ ] **Step 6: Build the GUI and run existing tests**

```bash
cd build
cmake .. -DQT_VERSION_MAJOR=6
make -j$(nproc)
```

Expected: compiles and GUI works as before.

- [ ] **Step 7: Commit**

```bash
git add src/data/wavedata.h src/data/wavedata.cpp src/data/wavemanager.h src/data/wavemanager.cpp
git commit -m "refactor: extract WaveData base class from WaveInstance (Qt-free)"
```

---

### Task 6: Update JsonRequest to extend JsonFileReader

**Files:**
- Modify: `src/util/jsonrequest.hpp`
- Modify: `src/util/jsonrequest.cpp`

- [ ] **Step 1: Make JsonRequest inherit from JsonFileReader**

In `src/util/jsonrequest.hpp`, add include and change inheritance:

```cpp
#include "jsonfilereader.h"

class StreamRequest : public QObject,
                      public std::stringstream
{
    // ... unchanged
};

class JsonRequest : public StreamRequest,
                    public JsonFileReader
{
    Q_OBJECT
    set_tracked();

public:
    JsonRequest(const std::string& path, bool bWarn = true);
    // bValid and data are inherited from JsonFileReader
};
```

Note: Since `JsonRequest` now inherits `bValid` and `data` from `JsonFileReader`, remove the direct declarations if they exist. The constructor should populate the inherited fields.

- [ ] **Step 2: Update JsonRequest constructor**

In `src/util/jsonrequest.cpp`, update the constructor to populate inherited fields:

```cpp
JsonRequest::JsonRequest(const std::string& path, bool bWarn) : StreamRequest(path)
{
    if (fail() || bad())
    {
        QWARNING(!bWarn, "Could not parse: " << path, return);
        return;
    }
    this->data = nlohmann::json::parse(*this);
    this->bValid = true;
}
```

This is essentially the same code but now `bValid` and `data` are from `JsonFileReader`.

- [ ] **Step 3: Build and verify**

```bash
cd build
make -j$(nproc)
```

Expected: compiles. The GUI still works.

- [ ] **Step 4: Commit**

```bash
git add src/util/jsonrequest.hpp src/util/jsonrequest.cpp
git commit -m "refactor: make JsonRequest extend JsonFileReader"
```

---

### Task 7: Wire CLI target into CMake

**Files:**
- Create: `src/headless/CMakeLists.txt`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create src/headless/ directory**

```bash
mkdir -p src/headless
```

- [ ] **Step 2: Create the CLI CMakeLists.txt**

```cmake
# src/headless/CMakeLists.txt
# Headless CLI binary — no Qt dependency

set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)

add_executable(rocprof-compute-viewer-cli
    main.cpp
    headless_dispatcher.cpp
    session_cache.cpp
    cmd_info.cpp
    cmd_isa.cpp
    cmd_waves.cpp
    cmd_occupancy.cpp
    cmd_latency.cpp
    cmd_counters.cpp
    cmd_perfcounters.cpp
    # Shared Qt-free sources
    ${CMAKE_SOURCE_DIR}/src/analysis/latency.cpp
    ${CMAKE_SOURCE_DIR}/src/analysis/derived_counter.cpp
    ${CMAKE_SOURCE_DIR}/src/code/codeload.cpp
    ${CMAKE_SOURCE_DIR}/src/data/shaderdata.cpp
    ${CMAKE_SOURCE_DIR}/src/data/wavedata.cpp
    ${CMAKE_SOURCE_DIR}/src/util/jsonfilereader.cpp
)

target_include_directories(rocprof-compute-viewer-cli PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json/include
    ${CMAKE_BINARY_DIR}/src  # for generated version.h
)

target_link_libraries(rocprof-compute-viewer-cli PRIVATE Threads::Threads)
target_compile_features(rocprof-compute-viewer-cli PRIVATE cxx_std_20)

install(TARGETS rocprof-compute-viewer-cli DESTINATION ${CMAKE_INSTALL_BINDIR})
```

- [ ] **Step 3: Add subdirectory to root CMakeLists.txt**

At the end of `CMakeLists.txt` (before the final `endif()`), add:

```cmake
option(BUILD_CLI "Build the headless CLI binary" ON)
if(BUILD_CLI)
    add_subdirectory(src/headless)
endif()
```

- [ ] **Step 4: Create stub main.cpp to verify the build**

```cpp
// src/headless/main.cpp
#include <iostream>

int main(int argc, char* argv[])
{
    std::cout << "rocprof-compute-viewer-cli stub" << std::endl;
    return 0;
}
```

Also create empty stubs for all source files listed in CMakeLists.txt so it compiles:

```bash
for f in headless_dispatcher.cpp session_cache.cpp cmd_info.cpp cmd_isa.cpp cmd_waves.cpp cmd_occupancy.cpp cmd_latency.cpp cmd_counters.cpp cmd_perfcounters.cpp; do
    touch src/headless/$f
done
```

- [ ] **Step 5: Build to verify the CLI target links without Qt**

```bash
cd build
cmake .. -DQT_VERSION_MAJOR=6
make -j$(nproc) rocprof-compute-viewer-cli
```

Expected: the CLI binary compiles and links. Run `ldd build/rocprof-compute-viewer-cli` (Linux) to verify no Qt libraries in the link.

- [ ] **Step 6: Commit**

```bash
git add src/headless/CMakeLists.txt src/headless/main.cpp CMakeLists.txt
git add src/headless/headless_dispatcher.cpp src/headless/session_cache.cpp
git add src/headless/cmd_*.cpp
git commit -m "feat(headless): add CLI CMake target and stub entry point"
```

---

## Phase 2: CLI Infrastructure

### Task 8: JSON Output Helpers

**Files:**
- Create: `src/headless/json_output.h`

- [ ] **Step 1: Create the json_output helper**

```cpp
// src/headless/json_output.h
#pragma once

#include <iostream>
#include <string>
#include "json/include/nlohmann/json.hpp"

namespace Headless {

/// Write a successful JSON result to stdout.
/// @param command  The subcommand name (e.g. "info", "isa").
/// @param uiOutputDir  The ui_output directory path.
/// @param data  The command-specific data payload.
/// @param offset  Pagination offset (-1 = no pagination).
/// @param limit  Pagination limit (-1 = no pagination).
/// @param total  Total item count (-1 = no pagination).
inline void writeJson(const std::string& command,
                      const std::string& uiOutputDir,
                      const nlohmann::json& data,
                      int offset = -1, int limit = -1, int total = -1)
{
    nlohmann::json envelope;
    envelope["version"] = "1.0.0";
    envelope["command"] = command;
    envelope["ui_output_dir"] = uiOutputDir;
    if (offset >= 0 || limit >= 0)
    {
        envelope["pagination"]["offset"] = offset >= 0 ? offset : 0;
        envelope["pagination"]["limit"] = limit >= 0 ? limit : total;
        envelope["pagination"]["total"] = total;
    }
    envelope["data"] = data;
    std::cout << envelope.dump(2) << std::endl;
}

/// Write a compact JSON result to stdout (for interactive mode, one line).
inline void writeJsonCompact(const std::string& command,
                             const std::string& uiOutputDir,
                             const nlohmann::json& data,
                             int offset = -1, int limit = -1, int total = -1)
{
    nlohmann::json envelope;
    envelope["version"] = "1.0.0";
    envelope["command"] = command;
    envelope["ui_output_dir"] = uiOutputDir;
    if (offset >= 0 || limit >= 0)
    {
        envelope["pagination"]["offset"] = offset >= 0 ? offset : 0;
        envelope["pagination"]["limit"] = limit >= 0 ? limit : total;
        envelope["pagination"]["total"] = total;
    }
    envelope["data"] = data;
    std::cout << envelope.dump(-1) << std::endl;
}

/// Write a JSON error to stderr.
inline void writeError(const std::string& message)
{
    nlohmann::json err;
    err["error"] = message;
    std::cerr << err.dump(-1) << std::endl;
}

} // namespace Headless
```

- [ ] **Step 2: Commit**

```bash
git add src/headless/json_output.h
git commit -m "feat(headless): add JSON output envelope and error helpers"
```

---

### Task 9: Headless Dispatcher and Arg Parsing

**Files:**
- Create: `src/headless/headless_dispatcher.h`
- Write: `src/headless/headless_dispatcher.cpp`

- [ ] **Step 1: Create the dispatcher header**

```cpp
// src/headless/headless_dispatcher.h
#pragma once

#include <string>
#include <vector>

namespace Headless {

struct HeadlessArgs
{
    std::string command;
    std::string uiOutputDir;
    std::vector<std::string> options;
    int limit = -1;   // -1 = no limit
    int offset = 0;
    bool interactive = false;
    bool compact = false; // true in interactive mode
};

/// Parse argv into HeadlessArgs.
HeadlessArgs parseArgs(int argc, char* argv[]);

/// Parse a single interactive-mode command line into HeadlessArgs.
/// uiOutputDir is carried from the session.
HeadlessArgs parseInteractiveLine(const std::string& line,
                                  const std::string& uiOutputDir);

/// Dispatch a parsed command. Returns exit code.
int dispatch(const HeadlessArgs& args);

/// Run interactive mode: read commands from stdin, dispatch each.
int runInteractive(const std::string& uiOutputDir);

// Option parsing helpers
std::string getOption(const std::vector<std::string>& options,
                      const std::string& flag,
                      const std::string& defaultValue = "");
bool hasFlag(const std::vector<std::string>& options, const std::string& flag);

} // namespace Headless
```

- [ ] **Step 2: Implement the dispatcher**

```cpp
// src/headless/headless_dispatcher.cpp
#include "headless_dispatcher.h"
#include "json_output.h"
#include "session_cache.h"
#include "util/version.h"

#include "cmd_info.h"
#include "cmd_isa.h"
#include "cmd_waves.h"
#include "cmd_occupancy.h"
#include "cmd_latency.h"
#include "cmd_counters.h"
#include "cmd_perfcounters.h"

#include <algorithm>
#include <iostream>
#include <sstream>

namespace Headless {

static SessionCache g_cache;

std::string getOption(const std::vector<std::string>& options,
                      const std::string& flag,
                      const std::string& defaultValue)
{
    auto it = std::find(options.begin(), options.end(), flag);
    if (it != options.end() && (it + 1) != options.end())
        return *(it + 1);
    return defaultValue;
}

bool hasFlag(const std::vector<std::string>& options, const std::string& flag)
{
    return std::find(options.begin(), options.end(), flag) != options.end();
}

HeadlessArgs parseArgs(int argc, char* argv[])
{
    HeadlessArgs args;

    if (argc < 2) return args;

    int i = 1;

    // Check for global flags first
    while (i < argc)
    {
        std::string arg(argv[i]);
        if (arg == "--version")
        {
            auto& v = Version::Get();
            std::cout << "rocprof-compute-viewer-cli "
                      << v.viewer_major << "." << v.viewer_minor << "." << v.viewer_rev
                      << std::endl;
            std::exit(0);
        }
        else if (arg == "--help")
        {
            args.command = "help";
            return args;
        }
        else if (arg == "--interactive")
        {
            args.interactive = true;
            args.compact = true;
            i++;
        }
        else if (arg == "--limit")
        {
            if (i + 1 < argc) args.limit = std::stoi(argv[++i]);
            i++;
        }
        else if (arg == "--offset")
        {
            if (i + 1 < argc) args.offset = std::stoi(argv[++i]);
            i++;
        }
        else break;
    }

    // If interactive, remaining arg is ui_output_dir
    if (args.interactive)
    {
        if (i < argc) args.uiOutputDir = argv[i];
        return args;
    }

    // Non-interactive: next arg is command
    if (i < argc) args.command = argv[i++];

    // Collect remaining args; last non-option is ui_output_dir
    std::vector<std::string> remaining;
    while (i < argc) remaining.push_back(argv[i++]);

    if (!remaining.empty() && remaining.back()[0] != '-')
    {
        args.uiOutputDir = remaining.back();
        remaining.pop_back();
    }

    args.options = std::move(remaining);
    return args;
}

HeadlessArgs parseInteractiveLine(const std::string& line,
                                  const std::string& uiOutputDir)
{
    HeadlessArgs args;
    args.uiOutputDir = uiOutputDir;
    args.compact = true;

    std::istringstream iss(line);
    std::string token;
    std::vector<std::string> tokens;
    while (iss >> token) tokens.push_back(token);

    if (tokens.empty()) return args;

    args.command = tokens[0];
    for (size_t i = 1; i < tokens.size(); i++)
    {
        if (tokens[i] == "--limit" && i + 1 < tokens.size())
            args.limit = std::stoi(tokens[++i]);
        else if (tokens[i] == "--offset" && i + 1 < tokens.size())
            args.offset = std::stoi(tokens[++i]);
        else
            args.options.push_back(tokens[i]);
    }

    return args;
}

static void printUsage()
{
    std::cerr
        << "Usage: rocprof-compute-viewer-cli <command> [options] <ui_output_dir>\n"
        << "       rocprof-compute-viewer-cli --interactive <ui_output_dir>\n"
        << "\n"
        << "Commands:\n"
        << "  info          Manifest metadata (gfxip, topology, counter names)\n"
        << "  isa           Instruction table with metrics and source mapping\n"
        << "  waves         Per-wave instruction traces and timeline states\n"
        << "  occupancy     Wave occupancy data (--level se|cu|slot)\n"
        << "  latency       Memory instruction latency estimates\n"
        << "  counters      Derived counter expression evaluation\n"
        << "  perfcounters  Raw hardware counter samples per SE\n"
        << "\n"
        << "Global options:\n"
        << "  --help        Show this help\n"
        << "  --version     Show version\n"
        << "  --interactive Enter interactive mode\n"
        << "  --limit N     Max items in output array\n"
        << "  --offset N    Skip first N items\n";
}

int dispatch(const HeadlessArgs& args)
{
    if (args.command.empty() || args.command == "help")
    {
        printUsage();
        return args.command == "help" ? 0 : 1;
    }

    if (args.uiOutputDir.empty())
    {
        writeError("ui_output_dir is required");
        return 1;
    }

    if (args.command == "info")         return cmdInfo(args, g_cache);
    if (args.command == "isa")          return cmdIsa(args, g_cache);
    if (args.command == "waves")        return cmdWaves(args, g_cache);
    if (args.command == "occupancy")    return cmdOccupancy(args, g_cache);
    if (args.command == "latency")      return cmdLatency(args, g_cache);
    if (args.command == "counters")     return cmdCounters(args, g_cache);
    if (args.command == "perfcounters") return cmdPerfcounters(args, g_cache);

    writeError("unknown command: " + args.command);
    printUsage();
    return 1;
}

int runInteractive(const std::string& uiOutputDir)
{
    // Cache is shared across commands in interactive mode
    std::string line;
    while (std::getline(std::cin, line))
    {
        // Trim whitespace
        auto start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        line = line.substr(start);

        if (line == "quit" || line == "exit") break;

        auto args = parseInteractiveLine(line, uiOutputDir);
        dispatch(args);
    }
    return 0;
}

} // namespace Headless
```

- [ ] **Step 3: Commit**

```bash
git add src/headless/headless_dispatcher.h src/headless/headless_dispatcher.cpp
git commit -m "feat(headless): add dispatcher with arg parsing and interactive mode"
```

---

### Task 10: SessionCache

**Files:**
- Create: `src/headless/session_cache.h`
- Write: `src/headless/session_cache.cpp`

- [ ] **Step 1: Create the SessionCache header**

```cpp
// src/headless/session_cache.h
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>
#include "code/codeload.hpp"
#include "data/shaderdata.h"
#include "data/wavedata.h"
#include "json/include/nlohmann/json.hpp"

namespace Headless {

/// In-memory cache for parsed data. In single-command mode, each invocation
/// creates a fresh SessionCache. In interactive mode, the same instance
/// persists across commands, avoiding redundant JSON parsing.
struct SessionCache
{
    // Manifest
    nlohmann::json manifest;
    bool manifest_loaded = false;

    // Code data
    std::vector<CodeData> code;
    bool code_loaded = false;

    // Wave data (keyed by file path)
    std::map<std::string, std::shared_ptr<WaveData>> waves;

    // Shader data
    std::unique_ptr<ShaderDataManager> shaderdata;

    // Occupancy
    nlohmann::json occupancy;
    bool occupancy_loaded = false;

    // Realtime clock
    nlohmann::json realtime;
    bool realtime_loaded = false;

    /// Ensure manifest is loaded. Returns reference.
    const nlohmann::json& loadManifest(const std::string& uiOutputDir);

    /// Ensure code.json is loaded. Returns reference.
    const std::vector<CodeData>& loadCode(const std::string& uiOutputDir);

    /// Load a wave file, caching by path.
    std::shared_ptr<WaveData> loadWave(const std::string& wavePath);

    /// Ensure shaderdata is loaded. Returns reference.
    ShaderDataManager& loadShaderData(const std::string& uiOutputDir);

    /// Ensure occupancy.json is loaded. Returns reference.
    const nlohmann::json& loadOccupancy(const std::string& uiOutputDir);

    /// Ensure realtime.json is loaded. Returns reference.
    const nlohmann::json& loadRealtime(const std::string& uiOutputDir);
};

} // namespace Headless
```

- [ ] **Step 2: Implement SessionCache**

```cpp
// src/headless/session_cache.cpp
#include "session_cache.h"
#include "util/jsonfilereader.h"
#include <filesystem>

namespace Headless {

const nlohmann::json& SessionCache::loadManifest(const std::string& uiOutputDir)
{
    if (manifest_loaded) return manifest;

    JsonFileReader reader(uiOutputDir + "/filenames.json");
    if (!reader.bValid)
        throw std::runtime_error("filenames.json not found in " + uiOutputDir);

    manifest = std::move(reader.data);
    manifest_loaded = true;
    return manifest;
}

const std::vector<CodeData>& SessionCache::loadCode(const std::string& uiOutputDir)
{
    if (code_loaded) return code;

    code = CodeData::LoadCode(uiOutputDir + "/code.json");
    code_loaded = true;
    return code;
}

std::shared_ptr<WaveData> SessionCache::loadWave(const std::string& wavePath)
{
    auto it = waves.find(wavePath);
    if (it != waves.end()) return it->second;

    auto wd = std::make_shared<WaveData>();
    if (!wd->Load(wavePath))
        return nullptr;

    waves[wavePath] = wd;
    return wd;
}

ShaderDataManager& SessionCache::loadShaderData(const std::string& uiOutputDir)
{
    if (shaderdata) return *shaderdata;

    shaderdata = std::make_unique<ShaderDataManager>();
    auto& mf = loadManifest(uiOutputDir);
    if (mf.contains("shaderdata_filenames"))
        shaderdata->Load(mf["shaderdata_filenames"], uiOutputDir + "/");

    return *shaderdata;
}

const nlohmann::json& SessionCache::loadOccupancy(const std::string& uiOutputDir)
{
    if (occupancy_loaded) return occupancy;

    std::string path = uiOutputDir + "/occupancy.json";
    if (std::filesystem::exists(path))
    {
        JsonFileReader reader(path);
        if (reader.bValid) occupancy = std::move(reader.data);
    }
    occupancy_loaded = true;
    return occupancy;
}

const nlohmann::json& SessionCache::loadRealtime(const std::string& uiOutputDir)
{
    if (realtime_loaded) return realtime;

    std::string path = uiOutputDir + "/realtime.json";
    if (std::filesystem::exists(path))
    {
        JsonFileReader reader(path);
        if (reader.bValid) realtime = std::move(reader.data);
    }
    realtime_loaded = true;
    return realtime;
}

} // namespace Headless
```

- [ ] **Step 3: Commit**

```bash
git add src/headless/session_cache.h src/headless/session_cache.cpp
git commit -m "feat(headless): add SessionCache for in-memory data caching"
```

---

### Task 11: Update main.cpp entry point

**Files:**
- Write: `src/headless/main.cpp`

- [ ] **Step 1: Implement the CLI entry point**

```cpp
// src/headless/main.cpp
#include "headless_dispatcher.h"

int main(int argc, char* argv[])
{
    auto args = Headless::parseArgs(argc, argv);

    if (args.interactive)
    {
        if (args.uiOutputDir.empty())
        {
            Headless::writeError("ui_output_dir is required for interactive mode");
            return 1;
        }
        return Headless::runInteractive(args.uiOutputDir);
    }

    return Headless::dispatch(args);
}
```

- [ ] **Step 2: Commit**

```bash
git add src/headless/main.cpp
git commit -m "feat(headless): implement CLI entry point with interactive mode support"
```

---

## Phase 3: Command Implementations

Each command follows the same pattern: header declares `cmdX(args, cache)`, implementation loads data via cache, serializes to JSON, and writes via `writeJson()`.

### Task 12: Implement `info` command

**Files:**
- Create: `src/headless/cmd_info.h`
- Write: `src/headless/cmd_info.cpp`

- [ ] **Step 1: Create header**

```cpp
// src/headless/cmd_info.h
#pragma once
#include "headless_dispatcher.h"
#include "session_cache.h"

namespace Headless {
int cmdInfo(const HeadlessArgs& args, SessionCache& cache);
}
```

- [ ] **Step 2: Implement**

```cpp
// src/headless/cmd_info.cpp
#include "cmd_info.h"
#include "json_output.h"

namespace Headless {

int cmdInfo(const HeadlessArgs& args, SessionCache& cache)
{
    try
    {
        auto& manifest = cache.loadManifest(args.uiOutputDir);

        nlohmann::json data;
        data["gfxip"] = manifest.value("gfxip", 0);
        data["gfxv"] = manifest.value("gfxv", "");
        data["version"] = manifest.value("version", "");
        data["pc_sampling"] = manifest.value("pc_sampling", false);
        data["thread_trace"] = manifest.value("thread_trace", false);

        if (manifest.contains("counter_names"))
            data["counter_names"] = manifest["counter_names"];

        int numSe = 0, numWaves = 0;
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

        if (args.compact)
            writeJsonCompact("info", args.uiOutputDir, data);
        else
            writeJson("info", args.uiOutputDir, data);
        return 0;
    }
    catch (const std::exception& e)
    {
        writeError(std::string("info: ") + e.what());
        return 1;
    }
}

} // namespace Headless
```

- [ ] **Step 3: Build and verify**

```bash
cd build && make -j$(nproc) rocprof-compute-viewer-cli
./rocprof-compute-viewer-cli info /path/to/ui_output/
```

Expected: JSON output with version envelope, gfxip, counter_names, num_se, num_waves.

- [ ] **Step 4: Commit**

```bash
git add src/headless/cmd_info.h src/headless/cmd_info.cpp
git commit -m "feat(headless): implement info command"
```

---

### Task 13: Implement `isa` command

**Files:**
- Create: `src/headless/cmd_isa.h`
- Write: `src/headless/cmd_isa.cpp`

- [ ] **Step 1: Create header**

```cpp
// src/headless/cmd_isa.h
#pragma once
#include "headless_dispatcher.h"
#include "session_cache.h"

namespace Headless {
int cmdIsa(const HeadlessArgs& args, SessionCache& cache);
}
```

- [ ] **Step 2: Implement**

Uses `CodeData::LoadCode` (now Qt-free via JsonFileReader + TokenTypeMap).

```cpp
// src/headless/cmd_isa.cpp
#include "cmd_isa.h"
#include "json_output.h"

namespace Headless {

int cmdIsa(const HeadlessArgs& args, SessionCache& cache)
{
    try
    {
        auto& codeData = cache.loadCode(args.uiOutputDir);

        int64_t minCycles = 0;
        std::string minCyclesStr = getOption(args.options, "--min-cycles");
        if (!minCyclesStr.empty()) minCycles = std::stoll(minCyclesStr);

        nlohmann::json instructions = nlohmann::json::array();

        for (auto& cd : codeData)
        {
            if (!cd.line) continue;
            auto& l = *cd.line;

            int64_t totalCycles = l.latency_sum + l.idle_sum + l.stall_sum;
            if (totalCycles < minCycles) continue;

            nlohmann::json instr;
            instr["index"] = l.index.load();
            instr["opcode"] = l.inst;
            instr["address"] = l.addr;
            instr["codeobj_id"] = l.codeobj_id;
            instr["source"] = l.cppline;
            instr["type"] = l.type.load();
            instr["hitcount"] = l.hitcount;
            instr["latency_cycles"] = l.latency_sum;
            instr["idle_cycles"] = l.idle_sum;
            instr["stall_cycles"] = l.stall_sum;
            instr["pc_samples"] = l.pcsamples;
            instr["pc_stalls"] = l.pcstalls;
            if (!l.stallreasons.empty())
                instr["stall_reasons"] = l.stallreasons;

            instructions.push_back(std::move(instr));
        }

        int total = static_cast<int>(instructions.size());
        int offset = args.offset;
        int limit = args.limit;

        // Apply pagination
        if (offset > 0 || limit >= 0)
        {
            int end = limit >= 0 ? std::min(offset + limit, total) : total;
            offset = std::min(offset, total);
            nlohmann::json page = nlohmann::json::array();
            for (int i = offset; i < end; i++)
                page.push_back(std::move(instructions[i]));
            instructions = std::move(page);
        }

        nlohmann::json data;
        data["instructions"] = std::move(instructions);

        if (args.compact)
            writeJsonCompact("isa", args.uiOutputDir, data, offset, limit, total);
        else
            writeJson("isa", args.uiOutputDir, data, offset, limit, total);
        return 0;
    }
    catch (const std::exception& e)
    {
        writeError(std::string("isa: ") + e.what());
        return 1;
    }
}

} // namespace Headless
```

- [ ] **Step 3: Build and test**

```bash
cd build && make -j$(nproc) rocprof-compute-viewer-cli
./rocprof-compute-viewer-cli isa /path/to/ui_output/
./rocprof-compute-viewer-cli isa /path/to/ui_output/ --min-cycles 100 --limit 10
```

- [ ] **Step 4: Commit**

```bash
git add src/headless/cmd_isa.h src/headless/cmd_isa.cpp
git commit -m "feat(headless): implement isa command"
```

---

### Task 14: Implement `waves` command

**Files:**
- Create: `src/headless/cmd_waves.h`
- Write: `src/headless/cmd_waves.cpp`

- [ ] **Step 1: Create header**

```cpp
// src/headless/cmd_waves.h
#pragma once
#include "headless_dispatcher.h"
#include "session_cache.h"

namespace Headless {
int cmdWaves(const HeadlessArgs& args, SessionCache& cache);
}
```

- [ ] **Step 2: Implement**

Uses `WaveData::Load()` via SessionCache. Iterates the `wave_filenames` tree from the manifest.

```cpp
// src/headless/cmd_waves.cpp
#include "cmd_waves.h"
#include "json_output.h"

namespace Headless {

int cmdWaves(const HeadlessArgs& args, SessionCache& cache)
{
    try
    {
        auto& manifest = cache.loadManifest(args.uiOutputDir);

        std::string filterSe = getOption(args.options, "--se");
        std::string filterCu = getOption(args.options, "--cu");
        std::string filterSimd = getOption(args.options, "--simd");

        nlohmann::json wavesArray = nlohmann::json::array();

        if (manifest.contains("wave_filenames") && !manifest["wave_filenames"].is_null())
        {
            auto& wf = manifest["wave_filenames"];
            for (auto& [seKey, simds] : wf.items())
            {
                if (!filterSe.empty() && seKey != filterSe) continue;
                for (auto& [simdKey, slots] : simds.items())
                {
                    if (!filterSimd.empty() && simdKey != filterSimd) continue;
                    for (auto& [slotKey, waveIds] : slots.items())
                    {
                        for (auto& [waveIdKey, fileEntry] : waveIds.items())
                        {
                            std::string waveFile = args.uiOutputDir + "/" + fileEntry[0].get<std::string>();
                            auto wd = cache.loadWave(waveFile);
                            if (!wd) continue;

                            if (!filterCu.empty() && std::to_string(wd->cu) != filterCu) continue;

                            nlohmann::json wave;
                            wave["se"] = seKey;
                            wave["simd"] = simdKey;
                            wave["slot"] = slotKey;
                            wave["wave_id"] = waveIdKey;
                            wave["file"] = fileEntry[0].get<std::string>();
                            wave["begin"] = wd->wave_begin;
                            wave["end"] = wd->wave_end;
                            wave["cu"] = wd->cu;
                            wave["instruction_count"] = static_cast<int>(wd->instructions.size());

                            // Include instruction summary (not full list — use pagination)
                            nlohmann::json instrArray = nlohmann::json::array();
                            for (auto& wi : wd->instructions)
                            {
                                instrArray.push_back({
                                    {"clock", wi.clock},
                                    {"type", wi.type},
                                    {"stall", wi.stall},
                                    {"cycles", wi.cycles},
                                    {"code_line", wi.code_line}
                                });
                            }
                            wave["instructions"] = std::move(instrArray);

                            // Timeline
                            nlohmann::json tlArray = nlohmann::json::array();
                            for (auto& tl : wd->timeline)
                                tlArray.push_back({{"clock", tl.clock}, {"state", tl.state}, {"duration", tl.duration}});
                            wave["timeline"] = std::move(tlArray);

                            // Info
                            nlohmann::json infoObj;
                            for (auto& wi : wd->wave_info)
                            {
                                infoObj[wi.name] = wi.value;
                                if (wi.stalls > 0) infoObj[wi.name + "_stall"] = wi.stalls;
                            }
                            wave["info"] = std::move(infoObj);

                            wavesArray.push_back(std::move(wave));
                        }
                    }
                }
            }
        }

        int total = static_cast<int>(wavesArray.size());
        int offset = args.offset;
        int limit = args.limit;

        if (offset > 0 || limit >= 0)
        {
            int end = limit >= 0 ? std::min(offset + limit, total) : total;
            offset = std::min(offset, total);
            nlohmann::json page = nlohmann::json::array();
            for (int i = offset; i < end; i++)
                page.push_back(std::move(wavesArray[i]));
            wavesArray = std::move(page);
        }

        nlohmann::json data;
        data["waves"] = std::move(wavesArray);

        if (args.compact)
            writeJsonCompact("waves", args.uiOutputDir, data, offset, limit, total);
        else
            writeJson("waves", args.uiOutputDir, data, offset, limit, total);
        return 0;
    }
    catch (const std::exception& e)
    {
        writeError(std::string("waves: ") + e.what());
        return 1;
    }
}

} // namespace Headless
```

- [ ] **Step 3: Build and test**

```bash
cd build && make -j$(nproc) rocprof-compute-viewer-cli
./rocprof-compute-viewer-cli waves /path/to/ui_output/ --se 0 --limit 2
```

- [ ] **Step 4: Commit**

```bash
git add src/headless/cmd_waves.h src/headless/cmd_waves.cpp
git commit -m "feat(headless): implement waves command"
```

---

### Task 15: Implement `occupancy` command

**Files:**
- Create: `src/headless/cmd_occupancy.h`
- Write: `src/headless/cmd_occupancy.cpp`

- [ ] **Step 1: Create header**

```cpp
// src/headless/cmd_occupancy.h
#pragma once
#include "headless_dispatcher.h"
#include "session_cache.h"

namespace Headless {
int cmdOccupancy(const HeadlessArgs& args, SessionCache& cache);
}
```

- [ ] **Step 2: Implement**

Reads `occupancy.json` directly. Format: top-level keys are SE numbers, each containing arrays of `[timestamp, cu, simd, slot, enable, kernel_id]`.

```cpp
// src/headless/cmd_occupancy.cpp
#include "cmd_occupancy.h"
#include "json_output.h"

namespace Headless {

int cmdOccupancy(const HeadlessArgs& args, SessionCache& cache)
{
    try
    {
        auto& occ = cache.loadOccupancy(args.uiOutputDir);
        if (occ.is_null() || occ.empty())
        {
            writeError("occupancy: occupancy.json not found or empty in " + args.uiOutputDir);
            return 1;
        }

        std::string filterSe = getOption(args.options, "--se");
        std::string filterCu = getOption(args.options, "--cu");
        std::string level = getOption(args.options, "--level", "se");

        nlohmann::json data;

        // Include dispatch names if available
        if (occ.contains("dispatches"))
            data["dispatches"] = occ["dispatches"];

        nlohmann::json events = nlohmann::json::array();

        for (auto& [key, entries] : occ.items())
        {
            if (key == "version" || key == "dispatches") continue;

            // key is SE number
            if (!filterSe.empty() && key != filterSe) continue;

            for (auto& event : entries)
            {
                if (event.size() < 6) continue;

                int cu_num = int(event[1]);
                if (!filterCu.empty() && std::to_string(cu_num) != filterCu) continue;

                nlohmann::json ev;
                ev["se"] = std::stoi(key);
                ev["timestamp"] = int64_t(event[0]);
                ev["cu"] = cu_num;
                ev["simd"] = int(event[2]);
                ev["slot"] = int(event[3]);
                ev["enable"] = int(event[4]);
                ev["kernel_id"] = int(event[5]);

                events.push_back(std::move(ev));
            }
        }

        int total = static_cast<int>(events.size());
        int offset = args.offset;
        int limit_val = args.limit;

        if (offset > 0 || limit_val >= 0)
        {
            int end = limit_val >= 0 ? std::min(offset + limit_val, total) : total;
            offset = std::min(offset, total);
            nlohmann::json page = nlohmann::json::array();
            for (int i = offset; i < end; i++)
                page.push_back(std::move(events[i]));
            events = std::move(page);
        }

        data["level"] = level;
        data["events"] = std::move(events);

        if (args.compact)
            writeJsonCompact("occupancy", args.uiOutputDir, data, offset, limit_val, total);
        else
            writeJson("occupancy", args.uiOutputDir, data, offset, limit_val, total);
        return 0;
    }
    catch (const std::exception& e)
    {
        writeError(std::string("occupancy: ") + e.what());
        return 1;
    }
}

} // namespace Headless
```

- [ ] **Step 3: Commit**

```bash
git add src/headless/cmd_occupancy.h src/headless/cmd_occupancy.cpp
git commit -m "feat(headless): implement occupancy command"
```

---

### Task 16: Implement `latency` command

**Files:**
- Create: `src/headless/cmd_latency.h`
- Write: `src/headless/cmd_latency.cpp`

- [ ] **Step 1: Create header**

```cpp
// src/headless/cmd_latency.h
#pragma once
#include "headless_dispatcher.h"
#include "session_cache.h"

namespace Headless {
int cmdLatency(const HeadlessArgs& args, SessionCache& cache);
}
```

- [ ] **Step 2: Implement**

Mirrors `tests/latency/latency_cli.cpp` — uses `LatencyAnalyzer` directly.

```cpp
// src/headless/cmd_latency.cpp
#include "cmd_latency.h"
#include "json_output.h"
#include "analysis/latency.hpp"
#include <filesystem>

namespace Headless {

int cmdLatency(const HeadlessArgs& args, SessionCache& cache)
{
    try
    {
        auto& manifest = cache.loadManifest(args.uiOutputDir);
        auto counterNames = manifest.value("counter_names", std::vector<std::string>{});

        if (counterNames.empty())
        {
            writeError("latency: no counter_names found in filenames.json");
            return 1;
        }

        // Build code map from code.json
        auto& codeData = cache.loadCode(args.uiOutputDir);
        std::map<int, std::string> codeMap;
        for (auto& cd : codeData)
        {
            if (!cd.line) continue;
            codeMap[cd.line->index.load()] = cd.line->inst;
        }

        std::string typeFilter = getOption(args.options, "--type", "all");

        using CT = LatencyAnalysis::CounterType;
        std::vector<std::pair<CT, std::string>> countersToAnalyze;

        if (typeFilter == "all" || typeFilter == "vmem")
            if (LatencyAnalysis::LatencyAnalyzer::hasCounter(counterNames, CT::VMEM))
                countersToAnalyze.push_back({CT::VMEM, "SQ_INST_LEVEL_VMEM"});
        if (typeFilter == "all" || typeFilter == "lds")
            if (LatencyAnalysis::LatencyAnalyzer::hasCounter(counterNames, CT::LDS))
                countersToAnalyze.push_back({CT::LDS, "SQ_INST_LEVEL_LDS"});
        if (typeFilter == "all" || typeFilter == "smem")
            if (LatencyAnalysis::LatencyAnalyzer::hasCounter(counterNames, CT::SMEM))
                countersToAnalyze.push_back({CT::SMEM, "SQ_INST_LEVEL_SMEM"});

        int numSe = manifest.contains("wave_filenames") && !manifest["wave_filenames"].is_null()
                        ? static_cast<int>(manifest["wave_filenames"].size())
                        : 0;

        nlohmann::json results = nlohmann::json::array();

        for (auto& [ct, counterName] : countersToAnalyze)
        {
            LatencyAnalysis::LatencyAnalyzer analyzer(counterNames, codeMap, counterName, -1);

            for (int se = 0; se < numSe; ++se)
            {
                std::string perfFile = args.uiOutputDir + "/se" + std::to_string(se) + "_perfcounter.json";
                if (!std::filesystem::exists(perfFile)) continue;

                auto waveFilePaths = LatencyAnalysis::LatencyAnalyzer::collectWaveFilePaths(
                    args.uiOutputDir, se);
                if (!waveFilePaths.empty())
                    analyzer.analyzeFiles(perfFile, waveFilePaths);
            }

            auto instrResults = analyzer.getResults();
            for (auto& [codeIndex, instr] : instrResults)
            {
                if (instr.latencies.empty()) continue;
                auto stats = LatencyAnalysis::computeLatencyStats(instr.latencies, 40);

                nlohmann::json entry;
                entry["instruction"] = instr.code;
                entry["code_index"] = codeIndex;
                entry["type"] = LatencyAnalysis::counterTypeToString(ct);
                entry["count"] = stats.count;
                entry["mean_cycles"] = stats.mean;
                entry["stddev_cycles"] = stats.stdDev;
                entry["error"] = stats.error;
                entry["mean_issue"] = stats.meanIssue;
                entry["mean_stall"] = stats.meanStall;
                entry["total_issue"] = instr.totalIssue;
                entry["total_stall"] = instr.totalStall;
                results.push_back(std::move(entry));
            }
        }

        nlohmann::json data;
        data["results"] = std::move(results);

        if (args.compact)
            writeJsonCompact("latency", args.uiOutputDir, data);
        else
            writeJson("latency", args.uiOutputDir, data);
        return 0;
    }
    catch (const std::exception& e)
    {
        writeError(std::string("latency: ") + e.what());
        return 1;
    }
}

} // namespace Headless
```

- [ ] **Step 3: Commit**

```bash
git add src/headless/cmd_latency.h src/headless/cmd_latency.cpp
git commit -m "feat(headless): implement latency command using LatencyAnalyzer"
```

---

### Task 17: Implement `perfcounters` command

**Files:**
- Create: `src/headless/cmd_perfcounters.h`
- Write: `src/headless/cmd_perfcounters.cpp`

- [ ] **Step 1: Create header**

```cpp
// src/headless/cmd_perfcounters.h
#pragma once
#include "headless_dispatcher.h"
#include "session_cache.h"

namespace Headless {
int cmdPerfcounters(const HeadlessArgs& args, SessionCache& cache);
}
```

- [ ] **Step 2: Implement**

Reads `se*_perfcounter.json` directly. Each row is `[timestamp, c1, c2, c3, c4, cu, bank]`.

```cpp
// src/headless/cmd_perfcounters.cpp
#include "cmd_perfcounters.h"
#include "json_output.h"
#include "util/jsonfilereader.h"
#include <filesystem>

namespace Headless {

int cmdPerfcounters(const HeadlessArgs& args, SessionCache& cache)
{
    try
    {
        auto& manifest = cache.loadManifest(args.uiOutputDir);
        std::string filterSe = getOption(args.options, "--se");

        nlohmann::json data;
        if (manifest.contains("counter_names"))
            data["counter_names"] = manifest["counter_names"];

        data["engines"] = nlohmann::json::array();

        for (auto& entry : std::filesystem::directory_iterator(args.uiOutputDir))
        {
            auto filename = entry.path().filename().string();
            if (filename.find("_perfcounter.json") == std::string::npos) continue;

            auto sePos = filename.find("se");
            auto underPos = filename.find('_');
            if (sePos == std::string::npos || underPos == std::string::npos) continue;
            std::string seStr = filename.substr(sePos + 2, underPos - sePos - 2);

            if (!filterSe.empty() && seStr != filterSe) continue;

            JsonFileReader reader(entry.path().string());
            if (!reader.bValid) continue;

            nlohmann::json engine;
            engine["se"] = std::stoi(seStr);
            engine["samples"] = reader.data.value("data", nlohmann::json::array());
            data["engines"].push_back(std::move(engine));
        }

        if (args.compact)
            writeJsonCompact("perfcounters", args.uiOutputDir, data);
        else
            writeJson("perfcounters", args.uiOutputDir, data);
        return 0;
    }
    catch (const std::exception& e)
    {
        writeError(std::string("perfcounters: ") + e.what());
        return 1;
    }
}

} // namespace Headless
```

- [ ] **Step 3: Commit**

```bash
git add src/headless/cmd_perfcounters.h src/headless/cmd_perfcounters.cpp
git commit -m "feat(headless): implement perfcounters command"
```

---

### Task 18: Implement `counters` command

**Files:**
- Create: `src/headless/cmd_counters.h`
- Write: `src/headless/cmd_counters.cpp`

This is the most complex command — it must build 4D tensors from perfcounter JSON data and populate `CounterContext`.

- [ ] **Step 1: Create header**

```cpp
// src/headless/cmd_counters.h
#pragma once
#include "headless_dispatcher.h"
#include "session_cache.h"

namespace Headless {
int cmdCounters(const HeadlessArgs& args, SessionCache& cache);
}
```

- [ ] **Step 2: Implement**

The tensor building logic is extracted from `CounterPlotView::buildDerivedManager()` in `src/graphics/specialized_plots.cpp:360-458`.

```cpp
// src/headless/cmd_counters.cpp
#include "cmd_counters.h"
#include "json_output.h"
#include "analysis/derived_counter.h"
#include "util/jsonfilereader.h"
#include <filesystem>
#include <fstream>
#include <set>

namespace Headless {

/// Build raw counter tensors from se*_perfcounter.json files and register with context.
/// Mirrors CounterPlotView::buildDerivedManager() logic.
static bool buildCounterTensors(
    DerivedCounter::CounterContext& ctx,
    const std::string& uiOutputDir,
    const std::vector<std::string>& counterNames)
{
    // Discover SEs and load raw data
    struct RawRow { int64_t timestamp; float counters[4]; int cu; int bank; int se; };
    std::vector<RawRow> allRows;
    std::set<int> seSet, cuSet;
    int64_t minTime = INT64_MAX, maxTime = INT64_MIN;

    for (auto& entry : std::filesystem::directory_iterator(uiOutputDir))
    {
        auto fname = entry.path().filename().string();
        if (fname.find("_perfcounter.json") == std::string::npos) continue;

        auto sePos = fname.find("se");
        auto underPos = fname.find('_');
        if (sePos == std::string::npos) continue;
        int se = std::stoi(fname.substr(sePos + 2, underPos - sePos - 2));

        JsonFileReader reader(entry.path().string());
        if (!reader.bValid || !reader.data.contains("data")) continue;

        for (auto& row : reader.data["data"])
        {
            RawRow r;
            r.timestamp = int64_t(row[0]);
            r.counters[0] = float(row[1]);
            r.counters[1] = float(row[2]);
            r.counters[2] = float(row[3]);
            r.counters[3] = float(row[4]);
            r.cu = int(int8_t(row[5]));
            r.bank = int(int8_t(row[6])) & 1;
            r.se = se;
            allRows.push_back(r);
            seSet.insert(se);
            cuSet.insert(r.cu);
            minTime = std::min(minTime, r.timestamp);
            maxTime = std::max(maxTime, r.timestamp);
        }
    }

    if (allRows.empty()) return false;

    // Compute time step (delta) from minimum spacing
    std::vector<int64_t> timestamps;
    for (auto& r : allRows) timestamps.push_back(r.timestamp);
    std::sort(timestamps.begin(), timestamps.end());
    timestamps.erase(std::unique(timestamps.begin(), timestamps.end()), timestamps.end());

    int64_t delta = maxTime - minTime;
    for (size_t i = 1; i < timestamps.size(); i++)
    {
        int64_t d = timestamps[i] - timestamps[i - 1];
        if (d > 0) delta = std::min(delta, d);
    }
    if (delta <= 0) delta = 1;

    int numSe = static_cast<int>(seSet.size());
    int numCu = cuSet.empty() ? 1 : *cuSet.rbegin() + 1;
    int numSamples = static_cast<int>((maxTime - minTime) / delta) + 1;

    DerivedCounter::Shape shape(1, numSe, numCu, numSamples);

    // Number of counter banks determines total counters: counterNames.size()
    int numBanks = (static_cast<int>(counterNames.size()) + 3) / 4;
    int totalCounters = static_cast<int>(counterNames.size());

    // Create tensors
    std::vector<std::shared_ptr<DerivedCounter::Tensor>> tensors(totalCounters);
    for (int i = 0; i < totalCounters; i++)
        tensors[i] = std::make_shared<DerivedCounter::Tensor>(shape, 0.0f);

    // Fill tensors
    for (auto& r : allRows)
    {
        int timeIdx = static_cast<int>((r.timestamp - minTime) / delta);
        timeIdx = std::min(timeIdx, numSamples - 1);

        int baseCounter = r.bank * 4;
        for (int c = 0; c < 4 && baseCounter + c < totalCounters; c++)
        {
            tensors[baseCounter + c]->at(0, r.se, r.cu, timeIdx) += r.counters[c];
        }
    }

    // Register counters with context
    for (int i = 0; i < totalCounters; i++)
        ctx.setCounter(counterNames[i], tensors[i]);

    // Create SCLOCK tensor
    auto sclock = std::make_shared<DerivedCounter::Tensor>(
        DerivedCounter::Shape(1, 1, 1, numSamples), 0.0f);
    for (int i = 0; i < numSamples; i++)
        sclock->at(0, 0, 0, i) = static_cast<float>(minTime + i * delta);
    ctx.setCounter("SCLOCK", sclock);

    return true;
}

int cmdCounters(const HeadlessArgs& args, SessionCache& cache)
{
    try
    {
        bool listMode = hasFlag(args.options, "--list");
        std::string exprFilter = getOption(args.options, "--expr");
        std::string defPath = getOption(args.options, "--definitions");

        auto& manifest = cache.loadManifest(args.uiOutputDir);
        auto counterNames = manifest.value("counter_names", std::vector<std::string>{});

        DerivedCounter::DerivedCounterManager mgr;

        // Load definitions
        if (!defPath.empty())
            mgr.loadDefinitionsFromFile(defPath);

        if (listMode)
        {
            nlohmann::json data;
            data["derived_counters"] = mgr.derivedCounterNames();
            data["raw_counters"] = counterNames;

            if (args.compact)
                writeJsonCompact("counters", args.uiOutputDir, data);
            else
                writeJson("counters", args.uiOutputDir, data);
            return 0;
        }

        // Build tensors from perfcounter data
        if (!counterNames.empty())
            buildCounterTensors(mgr.context(), args.uiOutputDir, counterNames);

        // Evaluate
        std::vector<std::string> toEvaluate;
        if (!exprFilter.empty())
            toEvaluate.push_back(exprFilter);
        else
            toEvaluate = mgr.derivedCounterNames();

        nlohmann::json results = nlohmann::json::array();
        for (auto& name : toEvaluate)
        {
            nlohmann::json entry;
            entry["name"] = name;

            auto result = mgr.evaluate(name);
            if (result)
            {
                nlohmann::json shapeArr = nlohmann::json::array();
                for (int d = 0; d < 4; d++) shapeArr.push_back(result->shape(d));
                entry["shape"] = shapeArr;

                if (result->totalSize() == 1)
                    entry["value"] = (*result)[0];
                else
                {
                    std::vector<float> flat(result->totalSize());
                    for (size_t i = 0; i < flat.size(); i++) flat[i] = (*result)[i];
                    entry["data"] = flat;
                }
            }
            else
            {
                entry["error"] = "evaluation failed";
            }
            results.push_back(std::move(entry));
        }

        nlohmann::json data;
        data["expressions"] = std::move(results);

        if (args.compact)
            writeJsonCompact("counters", args.uiOutputDir, data);
        else
            writeJson("counters", args.uiOutputDir, data);
        return 0;
    }
    catch (const std::exception& e)
    {
        writeError(std::string("counters: ") + e.what());
        return 1;
    }
}

} // namespace Headless
```

- [ ] **Step 3: Build and test**

```bash
cd build && make -j$(nproc) rocprof-compute-viewer-cli
./rocprof-compute-viewer-cli counters /path/to/ui_output/ --list
./rocprof-compute-viewer-cli counters /path/to/ui_output/
```

- [ ] **Step 4: Commit**

```bash
git add src/headless/cmd_counters.h src/headless/cmd_counters.cpp
git commit -m "feat(headless): implement counters command with tensor building"
```

---

## Phase 4: Integration Testing and Documentation

### Task 19: Integration Test Script

**Files:**
- Create: `tests/headless/test_headless_cli.sh`

- [ ] **Step 1: Create the test script**

```bash
#!/bin/bash
# tests/headless/test_headless_cli.sh
# Usage: ./test_headless_cli.sh <rcv_cli_binary> <ui_output_dir>

set -e

RCV="$1"
UIDIR="$2"

if [ -z "$RCV" ] || [ -z "$UIDIR" ]; then
    echo "Usage: $0 <rcv_cli_binary> <ui_output_dir>"
    exit 1
fi

PASS=0
FAIL=0

run_test() {
    local name="$1"
    shift
    echo -n "TEST: $name ... "
    if output=$("$@" 2>/dev/null); then
        if echo "$output" | python3 -c "
import sys, json
d = json.load(sys.stdin)
assert 'version' in d, 'missing version'
assert 'command' in d, 'missing command'
assert 'data' in d, 'missing data'
"; then
            echo "PASS"
            PASS=$((PASS + 1))
        else
            echo "FAIL (invalid JSON envelope)"
            FAIL=$((FAIL + 1))
        fi
    else
        echo "FAIL (exit code $?)"
        FAIL=$((FAIL + 1))
    fi
}

# Usage test
echo -n "TEST: no-args-shows-usage ... "
if "$RCV" 2>/dev/null; then echo "FAIL"; FAIL=$((FAIL + 1)); else echo "PASS"; PASS=$((PASS + 1)); fi

echo -n "TEST: unknown-command ... "
if "$RCV" bogus "$UIDIR" 2>/dev/null; then echo "FAIL"; FAIL=$((FAIL + 1)); else echo "PASS"; PASS=$((PASS + 1)); fi

echo -n "TEST: version ... "
if "$RCV" --version 2>/dev/null | grep -q "rocprof-compute-viewer-cli"; then echo "PASS"; PASS=$((PASS + 1)); else echo "FAIL"; FAIL=$((FAIL + 1)); fi

# Command tests
run_test "info" "$RCV" info "$UIDIR"
run_test "isa" "$RCV" isa "$UIDIR"
run_test "isa-limited" "$RCV" isa "$UIDIR" --limit 5
run_test "waves" "$RCV" waves "$UIDIR" --limit 2
run_test "occupancy" "$RCV" occupancy "$UIDIR"
run_test "latency" "$RCV" latency "$UIDIR" --type all
run_test "counters-list" "$RCV" counters "$UIDIR" --list
run_test "perfcounters" "$RCV" perfcounters "$UIDIR"

# Interactive mode test
echo -n "TEST: interactive ... "
IOUT=$(echo -e "info\nquit" | "$RCV" --interactive "$UIDIR" 2>/dev/null)
if echo "$IOUT" | python3 -c "import sys,json; d=json.loads(sys.stdin.readline()); assert d['command']=='info'"; then
    echo "PASS"; PASS=$((PASS + 1))
else
    echo "FAIL"; FAIL=$((FAIL + 1))
fi

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ] || exit 1
```

- [ ] **Step 2: Make executable**

```bash
chmod +x tests/headless/test_headless_cli.sh
```

- [ ] **Step 3: Run tests**

```bash
./tests/headless/test_headless_cli.sh ./build/rocprof-compute-viewer-cli /path/to/ui_output/
```

- [ ] **Step 4: Commit**

```bash
git add tests/headless/test_headless_cli.sh
git commit -m "test(headless): add integration test script"
```

---

### Task 20: SKILL.md for Agent Discovery

**Files:**
- Create: `SKILL.md`

- [ ] **Step 1: Create SKILL.md**

```markdown
---
name: "rcv-headless"
description: "Headless CLI for rocprof-compute-viewer. Analyzes GPU thread trace data from rocprofv3 and outputs results as JSON."
---

# rocprof-compute-viewer-cli

Command-line interface for analyzing GPU thread trace data without a GUI.

## Usage

Single command:
\`\`\`
rocprof-compute-viewer-cli <command> [options] <ui_output_dir>
\`\`\`

Interactive mode (in-memory caching across commands):
\`\`\`
rocprof-compute-viewer-cli --interactive <ui_output_dir>
\`\`\`

## Commands

| Command | Description |
|---------|-------------|
| info | Manifest metadata: gfxip, counter names, wave count |
| isa | Instruction table with metrics and source mapping |
| waves | Per-wave instruction traces and timeline states |
| occupancy | Wave occupancy events |
| latency | Memory instruction latency estimates (mean, stddev) |
| counters | Derived counter expression evaluation |
| perfcounters | Raw hardware counter samples per SE |

## Global Options

--help, --version, --interactive, --limit N, --offset N

## For AI Agents

1. All output is JSON to stdout, errors as JSON to stderr
2. Exit code 0 = success, non-zero = failure
3. Envelope: {"version","command","ui_output_dir","data"}
4. Use --interactive for multi-command sessions (avoids re-parsing)
5. Start with `info` to understand trace contents
6. Use `isa --min-cycles N` for instruction-level bottleneck identification
7. Use `latency --type vmem` to check memory-bound issues
8. Use `counters --list` to discover utilization metrics
```

- [ ] **Step 2: Commit**

```bash
git add SKILL.md
git commit -m "docs: add SKILL.md for AI agent discovery"
```

---

## Summary

| Task | Phase | What |
|------|-------|------|
| 1 | Refactor | JsonFileReader (Qt-free JSON loader) |
| 2 | Refactor | TokenTypeMap (Qt-free instruction classification) |
| 3 | Refactor | Decouple codeload.cpp from Qt |
| 4 | Refactor | Decouple shaderdata.cpp from Qt |
| 5 | Refactor | Extract WaveData from WaveInstance |
| 6 | Refactor | Make JsonRequest extend JsonFileReader |
| 7 | Infra | CMake target + stub entry point |
| 8 | Infra | JSON output helpers |
| 9 | Infra | Dispatcher + arg parsing |
| 10 | Infra | SessionCache |
| 11 | Infra | CLI main.cpp entry point |
| 12 | Command | info |
| 13 | Command | isa |
| 14 | Command | waves |
| 15 | Command | occupancy |
| 16 | Command | latency |
| 17 | Command | perfcounters |
| 18 | Command | counters (with tensor building) |
| 19 | Test | Integration test script |
| 20 | Docs | SKILL.md |
