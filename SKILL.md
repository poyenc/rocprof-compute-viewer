---
name: "rcv-headless"
description: "Headless CLI for rocprof-compute-viewer. Analyzes GPU thread trace data from rocprofv3 and outputs results as JSON."
---

# rocprof-compute-viewer-cli

Command-line interface for analyzing GPU thread trace data without a GUI.

## Usage

Single command:
```
rocprof-compute-viewer-cli <command> [options] <ui_output_dir>
```

Interactive mode (in-memory caching across commands):
```
rocprof-compute-viewer-cli --interactive <ui_output_dir>
```

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
