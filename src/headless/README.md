# rocprof-compute-viewer-cli

Headless CLI for analyzing GPU thread trace data from `rocprofv3`. Outputs structured JSON to stdout, suitable for both human inspection and programmatic consumption by AI agents.

No Qt dependency. Builds and runs on headless servers.

## Quick Start

```bash
# Build
cmake -B build -DBUILD_CLI=ON -DBUILD_GUI=OFF
cmake --build build --target rocprof-compute-viewer-cli

# Point at a rocprofv3 ui_output directory
rcv-cli summary ./ui_output_agent_0_dispatch_42/
```

## Commands

### summary

Single-shot performance overview. Start here.

```bash
rcv-cli summary ./ui_output/
rcv-cli summary --top 20 ./ui_output/     # more hotspots
```

Output includes:
- **session**: gfxip, num_se, num_waves
- **activity**: idle/stall/exec cycle counts and percentages
- **utilization**: VALU, MFMA, LDS, VMEM, FLAT, SCA, MISC, GPU (percentages)
- **throughput**: per-datatype TFLOPS (F16, BF16, F32, I8, etc.)
- **hotspots**: top-N instructions ranked by cycle consumption

Utilization and throughput require perfcounter data in the trace. If absent, only activity and hotspots are returned.

### info

Session metadata.

```bash
rcv-cli info ./ui_output/
```

Returns gfxip, counter names, wave count, and feature flags (pc_sampling, thread_trace).

### isa

ISA instruction table with per-instruction metrics.

```bash
rcv-cli isa ./ui_output/                              # all instructions
rcv-cli isa --sort cycles --top 20 ./ui_output/       # top 20 hotspots
rcv-cli isa --min-cycles 1000 ./ui_output/            # filter by cycle count
rcv-cli isa --sort hitcount --top 10 ./ui_output/     # most-executed instructions
```

Each instruction includes: index, opcode, address, source mapping, hitcount, cycles, idle, stall, pcsamples, pcstalls.

Valid `--sort` fields: `cycles`, `hitcount`, `stall`, `idle`, `pcsamples`, `pcstalls`.

### waves

Per-wave instruction traces with clock-level timing.

```bash
rcv-cli waves --se 0 --cu 3 --limit 5 ./ui_output/
rcv-cli waves --se 0 --simd 0 --limit 1 ./ui_output/
```

Each wave includes instructions (clock, type, stall, cycles, code_line), timeline (state durations), and info (per-type summaries).

### latency

Per-instruction memory latency statistics. Correlates wave instruction issue times with perfcounter level samples.

```bash
rcv-cli latency --type vmem ./ui_output/              # VMEM latency (default)
rcv-cli latency --type lds ./ui_output/               # LDS latency
rcv-cli latency --type smem ./ui_output/              # SMEM latency
rcv-cli latency --type vmem --cu 3 --se 0,1 ./ui_output/
```

Returns per-instruction statistics: mean, stddev, error, count, mean_issue, mean_stall.

### counters

Derived counter expression engine. Evaluates builtin and user-defined expressions over 4D tensors (XCC, SE, CU, Time).

```bash
rcv-cli counters ./ui_output/                         # evaluate all builtins
rcv-cli counters --list ./ui_output/                  # list available counters
rcv-cli counters --expr "mean[VALU_util]" ./ui_output/
rcv-cli counters --definitions my_counters.def ./ui_output/
rcv-cli counters --no-builtins --definitions custom.def ./ui_output/
```

Builtins include utilization percentages (VALU_util, MFMA_util, GPUutil, etc.) and TFLOPS throughput per datatype.

Definition file format (one per line):
```
my_metric := 100 * sum[ACTIVE_INST_VALU, axis=[XCC,SE,CU]] / sum[BUSY_CU_CYCLES, axis=[XCC,SE,CU]]
```

Available functions: `mean`, `max`, `min`, `sum` (with axis), `select`, `remove`, `delta`, `linear`.

### perfcounters

Raw hardware counter samples per shader engine.

```bash
rcv-cli perfcounters ./ui_output/
rcv-cli perfcounters --se 0 ./ui_output/
```

### occupancy

Wave occupancy events (launch/retire).

```bash
rcv-cli occupancy ./ui_output/
rcv-cli occupancy --se 0 --cu 3 ./ui_output/
```

## Output Format

All commands produce a JSON envelope on stdout:

```json
{
  "version": "1.0.0",
  "command": "summary",
  "ui_output_dir": "/path/to/ui_output",
  "data": { ... }
}
```

When `--limit`/`--offset` are used, a `pagination` field is added:

```json
{
  "pagination": { "offset": 0, "limit": 50, "total": 1523 },
  ...
}
```

Errors go to stderr as JSON:
```json
{"error": "description"}
```

Exit code 0 on success, non-zero on failure.

## Interactive Mode

For multi-command sessions (avoids re-parsing data files):

```bash
rcv-cli --interactive ./ui_output/
```

Protocol: one command per line on stdin, one compact JSON response per line on stdout. Parsed data is cached in memory across commands.

```
> info
{"version":"1.0.0","command":"info","ui_output_dir":"...","data":{...}}
> summary
{"version":"1.0.0","command":"summary","ui_output_dir":"...","data":{...}}
> isa --sort cycles --top 5
{"version":"1.0.0","command":"isa","ui_output_dir":"...","data":{...}}
> quit
```

Pipe-friendly:
```bash
echo -e "summary\nisa --sort cycles --top 10\nquit" | rcv-cli -i ./ui_output/
```

## Agent Kernel Optimization Loop

Typical workflow for an AI agent optimizing a HIP kernel:

```bash
# 1. Profile the kernel (external tool)
rocprofv3 --thread-trace -- ./my_kernel

# 2. Quick assessment: compute-bound or memory-bound?
rcv-cli summary ./ui_output/
# Check: MFMA_util vs VMEM_util, stall_pct, GPU utilization

# 3. Find instruction bottlenecks
rcv-cli isa --sort cycles --top 10 ./ui_output/
# Identify which instructions consume the most cycles

# 4. Deep dive on memory latency (if memory-bound)
rcv-cli latency --type vmem ./ui_output/
# Per-instruction VMEM latency: mean, stddev, count

# 5. Check achieved throughput
rcv-cli counters --expr "F16_TFLOPS" ./ui_output/

# 6. Edit kernel, re-profile, compare summaries
rcv-cli summary ./ui_output_v2/
```

For multi-query sessions, use interactive mode to avoid repeated file parsing:

```bash
rcv-cli -i ./ui_output/ <<'EOF'
summary
isa --sort cycles --top 10
latency --type vmem
counters --expr "F16_TFLOPS"
quit
EOF
```

## Global Options

| Option | Description |
|--------|-------------|
| `--help`, `-h` | Print help and exit |
| `--version`, `-v` | Print version and exit |
| `--interactive`, `-i` | Interactive REPL mode |
| `--compact` | Compact single-line JSON (auto-enabled in interactive mode) |
| `--limit N` | Limit number of items in output |
| `--offset N` | Skip first N items |

`--top` (on `isa` and `summary`) is applied before `--limit`/`--offset` pagination.

## Input Data

The CLI reads `ui_output` directories produced by `rocprofv3` thread trace collection:

| File | Contents |
|------|----------|
| `filenames.json` | Session manifest (gfxip, counter names, wave file tree) |
| `code.json` | ISA instruction table with PC sampling metrics |
| `se{N}_sm{M}_sl{S}_wv{W}.json` | Per-wave instruction traces |
| `se{N}_perfcounter.json` | Hardware counter time-series |
| `occupancy.json` | Wave occupancy events |
| `realtime.json` | GPU-clock-to-wall-clock mapping |

## Build

```bash
# CLI only (no Qt required)
cmake -B build -DBUILD_CLI=ON -DBUILD_GUI=OFF
cmake --build build --target rocprof-compute-viewer-cli

# Both GUI and CLI
cmake -B build -DBUILD_CLI=ON -DBUILD_GUI=ON
cmake --build build
```

Requires: C++20 compiler, CMake 3.16+, pthreads. No other dependencies (nlohmann/json is vendored).
