# RCV Headless CLI — Design Spec

## Problem

rocprof-compute-viewer (RCV) is a Qt-based desktop GUI for analyzing GPU thread trace data from rocprofv3. In an AI-driven kernel optimization workflow (write HIP kernel -> compile -> profile -> analyze -> iterate), the GUI is a bottleneck — an AI agent cannot interact with it programmatically.

Deploying the GUI binary on headless servers (CI nodes, HPC clusters) requires Qt libraries even when the GUI is never used. The analysis classes inside RCV are valuable but tightly coupled to the Qt-based UI layer.

## Goal

Build a separate `rocprof-compute-viewer-cli` binary that:

1. Outputs GPU thread trace analysis as JSON to stdout for AI agent consumption
2. Builds and links without any Qt dependency
3. Shares Qt-free analysis code with the GUI binary — no reimplementation

As a prerequisite, refactor the existing data-loading classes to decouple them from Qt, making them reusable across both binaries and future consumers (Python bindings, other tools).

## Non-Goals

- Reimplementing RCV's analysis logic in Python or any other language
- Modifying the existing GUI binary's user-facing behavior
- Designing the end-to-end AI optimization loop
- Disk-based caching (OS page cache + interactive mode's in-memory cache is sufficient)

## Architecture

### Two-binary, shared-core design

```
rocprof-compute-viewer (GUI)          rocprof-compute-viewer-cli (CLI)
         |                                       |
    QApplication                          src/headless/main.cpp
    MainWindow, widgets, plots            HeadlessDispatcher + cmd_* handlers
         |                                       |
         +------------- Shared Qt-free core -----+
         |                                       |
    JsonFileReader          (new, ifstream + nlohmann/json)
    CodeData::LoadCode      (refactored, uses JsonFileReader)
    ShaderDataManager       (refactored, uses JsonFileReader)
    WaveData                (new base, extracted from WaveInstance)
    TokenTypeMap            (new, instruction-prefix-to-type mapping)
    LatencyAnalyzer         (already Qt-free)
    DerivedCounterManager   (already Qt-free)
         |
    Qt-only layer (GUI binary only):
    JsonRequest             (extends JsonFileReader, adds QNetwork HTTP)
    WaveInstance             (extends WaveData + TokenGroup, adds Draw/rendering)
    Config                  (QColor, QSettings, delegates to TokenTypeMap)
    SESelector, plots, canvas, etc.
```

| Binary | Purpose | Qt required? |
|--------|---------|-------------|
| `rocprof-compute-viewer` | GUI (existing, unchanged behavior) | Yes |
| `rocprof-compute-viewer-cli` | Headless JSON output | No |

### Data flow

The CLI reads `ui_output_agent_*_dispatch_*` directories produced by rocprofv3. These contain well-structured JSON files:

| File | Contents |
|------|----------|
| `filenames.json` | Manifest: gfxip, counter_names, wave_filenames tree, shaderdata_filenames |
| `code.json` | ISA instruction table with PC sampling metrics |
| `se{N}_sm{M}_sl{S}_wv{W}.json` | Per-wave instruction traces, timeline, waitcnt, info |
| `se{N}_perfcounter.json` | Performance counter time-series (4 counters per bank per row) |
| `occupancy.json` | Wave occupancy events (slot enable/disable per SE/CU/SIMD/slot) |
| `realtime.json` | GPU-clock-to-wall-clock mapping per SE |
| Shaderdata files | Shader execution records per SE/CU/SIMD/slot |

## Refactoring: Qt-Free Core Extraction

Three refactors create the shared Qt-free core. Each is designed to be backward-compatible — the GUI binary continues to work identically.

### R1. JsonRequest → JsonFileReader base

**Current state:** `JsonRequest : StreamRequest : QObject, std::stringstream`. For local files, `StreamRequest` constructor uses `std::ifstream` internally. The QObject/QNetwork code path is HTTP-only.

**Refactor:**
- New `JsonFileReader` class (`src/util/jsonfilereader.h/.cpp`): opens file via `std::ifstream`, parses to `nlohmann::json data`, exposes `bool bValid`.
- `StreamRequest` / `JsonRequest` inherit from `JsonFileReader` and add HTTP support via QObject/QNetwork.
- Existing callers (`ShaderDataManager::Load`, `CodeData::LoadCode`) switch from `JsonRequest` to `JsonFileReader` for local file paths. This affects the GUI too — these callers always use local files, so `JsonFileReader` (ifstream) is functionally identical to what `JsonRequest` already does for local paths. No behavioral change. Other GUI code that needs HTTP (e.g., remote data loading) continues to use `JsonRequest`.

### R2. Config::CustomTokens → TokenTypeMap

**Current state:** `Config::CustomTokens()` returns `vector<pair<string,int>>` mapping instruction prefixes (`v_mfma`, `v_smfma`, `v_wmma`, `v_swmma`) to type indices. Depends on `Config::TokenColors()` which uses `QColor`.

**Refactor:**
- New `TokenTypeMap` (`src/util/tokentypemap.h`, header-only): contains the prefix-to-type-index mapping and default entries. Qt-free.
- `Config::CustomTokens()` delegates to `TokenTypeMap::defaults()` and layers on QColor-based customization from `token_def.json`.
- `codeload.cpp` uses `TokenTypeMap` directly instead of `Config::CustomTokens()`.

### R3. WaveInstance → WaveData base

**Current state:** `WaveInstance : TokenGroup`. The constructor loads wave JSON and populates `code`, `waitcnt`, `wave_info`, `line_to_clock`, plus parent's `tokens`, `timeline`. `TokenGroup` has `Draw(QPainter&)`.

**Refactor:**
- New `WaveData` class (`src/data/wavedata.h/.cpp`): holds plain C++ data fields:
  - `int64_t wave_begin, wave_end`
  - `int cu, wave_id`
  - `vector<CodeData> code`
  - `vector<WaveInstruction> instructions` — `{int64_t clock, int type, int stall, int cycles, int code_line}`
  - `vector<pair<int,int>> timeline` — `{state, duration}`
  - `vector<WaitCntEntry> waitcnt` — `{int code_line, vector<pair<int,int>> sources}`
  - `map<string, WaveInfoEntry> info` — `{string name, int64_t value, int64_t stall}`
- `WaveData::Load(path)` does the JSON parsing (moved from `WaveInstance` constructor).
- `WaveInstance` inherits from both `WaveData` and `TokenGroup`. Its constructor calls `WaveData::Load()`, then builds `Token` objects and mipmaps for rendering.
- The static `Get()` cache stays in `WaveInstance` (GUI optimization). `WaveData` has no singleton/cache.

### Decoupling principle

The refactored classes drop static singleton/cache patterns in the shared base. Caching is a GUI optimization (same wave file reloaded during scrolling) and belongs in the GUI layer (`WaveInstance::Get()`). The CLI and other consumers get stateless, reusable instances.

## CLI Interface

### Invocation

Two modes: single-command and interactive.

**Single-command mode** (one process per command):

```
rocprof-compute-viewer-cli <command> [options] <ui_output_dir>
```

**Interactive mode** (long-lived process, in-memory caching):

```
rocprof-compute-viewer-cli --interactive <ui_output_dir>
```

### Global Options

| Option | Description |
|--------|-------------|
| `--help` | Print usage and exit |
| `--version` | Print version string (from Qt-free `version.h`) and exit |
| `--interactive` | Enter interactive mode (see below) |
| `--limit N` | Max items in primary output array (default: all) |
| `--offset N` | Skip first N items (default: 0) |

`--limit` and `--offset` apply to the primary array in each command's output. This keeps output manageable for AI agent context windows, mirroring how GUI users scroll through data.

### Commands

| Command | Description | Primary array | Key Options |
|---------|-------------|---------------|-------------|
| `info` | Manifest metadata | — (single object) | — |
| `isa` | Instruction table with metrics | `instructions[]` | `--kernel <name>`, `--min-cycles N` |
| `waves` | Per-wave instruction traces | `waves[]` | `--se N`, `--cu N`, `--simd N` |
| `occupancy` | Wave occupancy events | `events[]` | `--level se\|cu\|slot`, `--se N`, `--cu N` |
| `latency` | Memory instruction latency | `results[]` | `--type vmem\|lds\|smem\|all` |
| `counters` | Derived counter evaluation | `expressions[]` | `--expr <name>`, `--list`, `--definitions <path>` |
| `perfcounters` | Raw hardware counter samples | `engines[].samples[]` | `--se N` |

### Example Usage

```bash
# What's in this trace?
rocprof-compute-viewer-cli info ./ui_output_agent_0_dispatch_42/

# Find instruction bottlenecks (paged)
rocprof-compute-viewer-cli isa ./ui_output_agent_0_dispatch_42/ --min-cycles 100 --limit 50

# Memory latency breakdown
rocprof-compute-viewer-cli latency ./ui_output_agent_0_dispatch_42/ --type vmem

# Utilization metrics
rocprof-compute-viewer-cli counters ./ui_output_agent_0_dispatch_42/ --list

# Wave traces for a specific CU, paged
rocprof-compute-viewer-cli waves ./ui_output_agent_0_dispatch_42/ --se 0 --cu 3 --limit 10
```

## Interactive Mode and Caching

### Motivation

An AI agent workflow is sequential — `info` → `isa` → `latency` → `counters` — all on the same `ui_output` directory. In single-command mode, each invocation re-parses the same JSON files (manifest, code, wave data). Interactive mode keeps the process alive so parsed data stays in memory across commands.

This is the same pattern used by language servers (LSP), MCP servers, and `sqlite3` interactive mode: long-lived process, commands over stdin, results on stdout.

### Protocol

```
$ rocprof-compute-viewer-cli --interactive ./ui_output_agent_0_dispatch_42/
> info
{"version":"1.0.0","command":"info","ui_output_dir":"...","data":{...}}
> isa --min-cycles 100 --limit 50
{"version":"1.0.0","command":"isa","ui_output_dir":"...","pagination":{...},"data":{...}}
> latency --type vmem
{"version":"1.0.0","command":"latency","ui_output_dir":"...","data":{...}}
> quit
```

- One command per line on stdin, same syntax as single-command arguments (minus the `ui_output_dir` which is fixed at startup)
- One JSON response per line on stdout (compact, no pretty-printing — newline-delimited JSON)
- Errors on stderr as JSON (same format as single-command mode)
- `quit` or EOF exits the process
- Pipe-friendly: `echo -e "info\nisa\nlatency --type vmem" | rcv-cli --interactive ./ui_output/`

### Caching Strategy

Interactive mode uses lazy, in-memory caching:

| Data | When cached | Scope |
|------|------------|-------|
| `filenames.json` (manifest) | On startup | Entire session |
| `code.json` (instruction table) | First command that needs it (`isa`, `latency`) | Entire session |
| Wave files | First `waves` command for each SE/SIMD/slot | Entire session |
| Perfcounter data | First `latency`, `counters`, or `perfcounters` command per SE | Entire session |
| `occupancy.json` | First `occupancy` command | Entire session |
| Derived counter tensors | First `counters` evaluation | Entire session, cleared if `--definitions` changes |

Parsed data is never written to disk. The cache lives only for the duration of the interactive session. Single-command mode has no caching (process exits after one command).

### Implementation

A `SessionCache` class holds shared pointers to parsed data:

```cpp
struct SessionCache {
    nlohmann::json manifest;                              // filenames.json
    std::vector<CodeData> code;                           // code.json
    std::map<std::string, std::shared_ptr<WaveData>> waves; // keyed by file path
    std::map<int, std::vector<PerfDataEntry>> perfdata;   // keyed by SE number
    nlohmann::json occupancy;                             // occupancy.json
    bool manifest_loaded = false;
    bool code_loaded = false;
    // ...
};
```

Each `cmd_*` handler receives a `SessionCache&`. In single-command mode, it's a fresh instance (no reuse). In interactive mode, the same instance is passed to all commands in the session.

## JSON Output Schema

### Envelope

Every command outputs a consistent envelope:

```json
{
  "version": "1.0.0",
  "command": "isa",
  "ui_output_dir": "/path/to/ui_output_agent_0_dispatch_42",
  "pagination": { "offset": 0, "limit": 50, "total": 1523 },
  "data": { ... }
}
```

- `version`: schema version for forward compatibility
- `pagination`: only present when `--limit` or `--offset` is used
- `data`: command-specific payload

### Error Output

Errors are written to stderr as JSON:

```json
{"error": "filenames.json not found in /path/to/dir"}
```

Exit code 0 on success, non-zero on failure.

### Schema Derivation

Each subcommand serializes the data structures from the shared Qt-free core:

- **Data extraction commands** (`info`, `isa`, `waves`, `occupancy`, `perfcounters`) — output mirrors the raw `ui_output` JSON structures with minimal reshaping.
- **Computation commands** (`latency`, `counters`) — output mirrors what `LatencyAnalyzer::getResults()` and `DerivedCounterManager::evaluate()` return.

Schemas are derived from C++ data structures, not designed independently. They will be documented after implementation.

## Counters: Definition Discovery and Data Loading

The `counters` command uses `DerivedCounterManager` (Qt-free) for expression evaluation but requires setup:

### Definition discovery

The GUI uses `QStandardPaths` to find `.def` files. The CLI provides:
- `--definitions <path>` flag for explicit definition file/directory
- Fallback to `$XDG_CONFIG_HOME/rocprof-compute-viewer/derived_counters/` (or `~/.config/`)
- Built-in defaults from `DerivedCounterManager` are always available

### Raw counter data loading

Before evaluating derived expressions, `CounterContext` must be populated with raw counter tensors via `setCounter()`. The GUI does this in `CounterPlotView::buildDerivedManager()` (specialized_plots.cpp:360-458).

The CLI replicates this logic Qt-free:

1. Scan `se*_perfcounter.json` files to discover SE count, CU count, and time range
2. Compute time step (delta) from minimum timestamp spacing
3. Allocate `(1, num_SEs, num_CUs, num_samples)` tensors per counter
4. Fill from JSON data: each row `[timestamp, c1, c2, c3, c4, cu, bank]` maps to tensor indices
5. Counter-name-to-column mapping: `counter_names[i]` maps to column `(i % 4) + 1` in bank `i / 4`
6. Create `SCLOCK` tensor: `(1, 1, 1, num_samples)` of time points
7. Optionally create `RCLOCK` from `realtime.json`
8. Register all via `context().setCounter(name, tensor_ptr)`

## Build Architecture

### CMake structure

The CLI is defined in `src/headless/CMakeLists.txt`, included from the root:

```cmake
# Root CMakeLists.txt:
option(BUILD_CLI "Build the headless CLI binary" ON)
if(BUILD_CLI)
    add_subdirectory(src/headless)
endif()
```

```cmake
# src/headless/CMakeLists.txt:
set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)

add_executable(rocprof-compute-viewer-cli
    main.cpp
    headless_dispatcher.cpp
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

`AUTOMOC`/`AUTOUIC`/`AUTORCC` are disabled in the subdirectory scope. The CLI gets `version.h` from the root's `configure_file` output. No Qt in `find_package` or `target_link_libraries`.

### Source classification

**Shared Qt-free core (linked into both binaries after refactoring):**

| File | Status | Notes |
|------|--------|-------|
| `src/analysis/latency.hpp/.cpp` | Already Qt-free | Proven by `tests/latency/latency_cli` |
| `src/analysis/derived_counter.h/.cpp` | Already Qt-free | Proven by `tests/counters/derived_counter_test` |
| `src/util/jsonfilereader.h/.cpp` | New | Extracted from `JsonRequest` |
| `src/util/tokentypemap.h` | New | Extracted from `Config::CustomTokens` |
| `src/data/wavedata.h/.cpp` | New | Extracted from `WaveInstance` |
| `src/code/codeload.hpp/.cpp` | Refactored | Switches from `JsonRequest` to `JsonFileReader`, from `Config` to `TokenTypeMap` |
| `src/data/shaderdata.h/.cpp` | Refactored | Switches from `JsonRequest` to `JsonFileReader` |
| `src/util/memtracker.h` | Already Qt-free | Header-only |
| `src/util/wave_utils.h` | Already Qt-free | Header-only |
| `src/util/version.h.in` | Already Qt-free | Generated by CMake |
| `src/json/` | Already Qt-free | Vendored nlohmann/json |

**Qt-dependent (GUI binary only, NOT linked into CLI):**

| File | Qt dependency |
|------|---------------|
| `src/util/jsonrequest.hpp/.cpp` | QObject, QNetworkAccessManager (extends JsonFileReader) |
| `src/data/wavemanager.h/.cpp` | QPainter, Canvas, TokenGroup (extends WaveData) |
| `src/config/config.hpp/.cpp` | QColor, QSettings, QApplication |
| `src/code/*.cpp` (except codeload) | Qt widgets |
| `src/graphics/`, `src/wave/`, `src/button/`, `src/summary/`, `src/collection/` | Full Qt |

## Agent Discovery: SKILL.md

A standalone markdown file shipped alongside the CLI binary at the repo root:

```yaml
---
name: "rcv-headless"
description: "Headless CLI for rocprof-compute-viewer. Analyzes GPU thread trace data as JSON."
---
```

Contents: command reference, usage examples, agent conventions (JSON stdout, exit codes, pagination, recommended command order).

## Key Design Decisions

1. **Separate binary, no Qt dependency** — A dedicated `rocprof-compute-viewer-cli` that never links Qt. Deployable on headless servers without Qt installation. The two binaries share a Qt-free core, keeping analysis logic in sync.

2. **Refactor-and-share over reimplementation** — Instead of parsing JSON files from scratch in the CLI, refactor three thin Qt boundaries (`JsonRequest`, `CustomTokens`, `WaveInstance`) to extract Qt-free bases. Both binaries share the same data-loading code. Changes to parsing logic benefit both.

3. **Decoupled, reusable analysis classes** — Refactored classes are stateless (no singletons, no caches in the shared base). The GUI layers caching on top via derived classes. The shared core is reusable by any consumer.

4. **Pagination for large data** — `--limit` / `--offset` on the primary output array, with pagination metadata in the JSON envelope. Mirrors how GUI users scroll through data and keeps AI agent context windows manageable.

5. **Schema derived from C++ types** — Don't design JSON schemas independently. Serialize what the analysis classes return. Document after implementation.

6. **No Python wrapper layer** — AI agents call `rocprof-compute-viewer-cli` directly.

7. **Fork-first, upstream later** — Prove the approach in a fork, then contribute back to ROCm/rocprof-compute-viewer.

8. **Existing precedent** — `tests/latency/latency_cli.cpp` proves `LatencyAnalyzer` works as a standalone Qt-free CLI. Its utility functions (`loadJson`, `loadCodeMap`, `loadCounterNames`) are reusable reference implementations. The headless CLI generalizes this pattern.

9. **Subdirectory CMake target** — `src/headless/CMakeLists.txt` is included via `add_subdirectory` from the root. Gets `version.h` for free, avoids AUTOMOC by setting it OFF in subdirectory scope. Mirrors the pattern used by `tests/`.

10. **Interactive mode with in-memory caching** — Instead of disk-based caching, the CLI supports `--interactive` mode where it stays alive and accepts commands on stdin. Parsed data (manifest, code, waves, perfcounters) is cached in memory across commands within a session. This follows the established pattern of LSP servers, MCP servers, and `sqlite3`. No cache invalidation complexity, no extra disk I/O. Single-command mode remains available for simple one-off queries.
