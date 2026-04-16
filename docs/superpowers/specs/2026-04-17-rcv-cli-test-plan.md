# RCV Headless CLI — Test Plan

## Test Data

### Generation

Profile an FMHA forward kernel on MI350 (gfx950) using rocprofv3 thread trace:

```bash
rocprofv3 --thread-trace -- <fmha_fwd_binary>
```

Select one `ui_output_agent_*_dispatch_*` directory as `$UIDIR` for all tests below.

### Prerequisite Checks

Before running CLI tests, verify the trace data is valid:

```bash
# Set test variables
RCV=./build/src/headless/rocprof-compute-viewer-cli
UIDIR=<path_to_ui_output_directory>

# Verify required files exist
test -f "$UIDIR/filenames.json" && echo "OK: manifest" || echo "MISSING: filenames.json"
test -f "$UIDIR/code.json" && echo "OK: code" || echo "MISSING: code.json"
ls "$UIDIR"/se*_perfcounter.json >/dev/null 2>&1 && echo "OK: perfcounters" || echo "MISSING: perfcounter files"
ls "$UIDIR"/se*_sm*.json >/dev/null 2>&1 && echo "OK: wave files" || echo "MISSING: wave files"

# Verify trace type
python3 -c "
import json
m = json.load(open('$UIDIR/filenames.json'))
assert m.get('thread_trace', False), 'Expected thread_trace=true'
print(f'gfxip={m.get(\"gfxip\")}, gfxv={m.get(\"gfxv\")}')
print(f'counter_names: {m.get(\"counter_names\", [])}')
print(f'thread_trace={m[\"thread_trace\"]}, pc_sampling={m.get(\"pc_sampling\", False)}')
"
```

---

## Part 1: Functional Tests

### 1.1 Global Options

```bash
# --version prints version string
$RCV --version
# Expected: "rocprof-compute-viewer-cli 0.1.7" (or current version)

# --help prints usage
$RCV --help
# Expected: usage text with all 7 commands listed

# No args exits non-zero
$RCV 2>/dev/null; echo "exit: $?"
# Expected: exit: 1

# Unknown command exits non-zero
$RCV bogus "$UIDIR" 2>/dev/null; echo "exit: $?"
# Expected: exit: 1
```

### 1.2 info Command

```bash
$RCV info "$UIDIR" | python3 -c "
import sys, json
d = json.load(sys.stdin)

# Envelope check
assert d['version'] == '1.0.0', f'version: {d[\"version\"]}'
assert d['command'] == 'info'
assert 'data' in d

data = d['data']
print(f'gfxip: {data.get(\"gfxip\")}')              # expect 950
print(f'gfxv: {data.get(\"gfxv\")}')                 # expect architecture string
print(f'thread_trace: {data.get(\"thread_trace\")}')  # expect True
print(f'num_se: {data.get(\"num_se\")}')              # expect > 0
print(f'num_waves: {data.get(\"num_waves\")}')         # expect > 0
print(f'counter_names: {data.get(\"counter_names\", [])}')

assert data['num_se'] > 0, 'Expected at least 1 shader engine'
assert data['num_waves'] > 0, 'Expected at least 1 wave'
print('PASS: info')
"
```

### 1.3 isa Command

```bash
# Basic isa
$RCV isa "$UIDIR" | python3 -c "
import sys, json
d = json.load(sys.stdin)
instrs = d['data']['instructions']
print(f'Total instructions: {len(instrs)}')
assert len(instrs) > 0, 'Expected instructions'

# Check fields on first instruction
i = instrs[0]
for field in ['index', 'opcode', 'address', 'hitcount', 'latency_cycles', 'idle_cycles', 'stall_cycles']:
    assert field in i, f'Missing field: {field}'
print(f'First: [{i[\"index\"]}] {i[\"opcode\"]}')
print('PASS: isa')
"

# isa with --min-cycles filter
$RCV isa "$UIDIR" --min-cycles 100 | python3 -c "
import sys, json
d = json.load(sys.stdin)
instrs = d['data']['instructions']
print(f'Instructions with >= 100 cycles: {len(instrs)}')
for i in instrs:
    total = i['latency_cycles'] + i['idle_cycles'] + i['stall_cycles']
    assert total >= 100, f'Instruction {i[\"index\"]} has only {total} cycles'
print('PASS: isa --min-cycles')
"

# isa with pagination
$RCV isa "$UIDIR" --limit 5 --offset 0 | python3 -c "
import sys, json
d = json.load(sys.stdin)
assert 'pagination' in d, 'Missing pagination'
p = d['pagination']
assert p['limit'] == 5
assert p['offset'] == 0
assert p['total'] > 0
assert len(d['data']['instructions']) <= 5
print(f'Page: {p[\"offset\"]}-{p[\"offset\"]+len(d[\"data\"][\"instructions\"])} of {p[\"total\"]}')
print('PASS: isa --limit')
"
```

### 1.4 waves Command

```bash
$RCV waves "$UIDIR" --limit 2 | python3 -c "
import sys, json
d = json.load(sys.stdin)
waves = d['data']['waves']
print(f'Waves returned: {len(waves)}')
assert len(waves) > 0, 'Expected at least 1 wave'
assert len(waves) <= 2, 'Pagination should limit to 2'

w = waves[0]
for field in ['se', 'simd', 'slot', 'begin', 'end', 'cu', 'instructions', 'timeline', 'info']:
    assert field in w, f'Missing field: {field}'
print(f'Wave: SE={w[\"se\"]} CU={w[\"cu\"]} instructions={len(w[\"instructions\"])}')
print('PASS: waves')
"

# waves with SE filter
$RCV waves "$UIDIR" --se 0 --limit 3 | python3 -c "
import sys, json
d = json.load(sys.stdin)
for w in d['data']['waves']:
    assert str(w['se']) == '0', f'Got SE={w[\"se\"]}, expected 0'
print('PASS: waves --se filter')
"
```

### 1.5 occupancy Command

```bash
$RCV occupancy "$UIDIR" --limit 20 | python3 -c "
import sys, json
d = json.load(sys.stdin)
events = d['data']['events']
print(f'Occupancy events: {len(events)}')
if len(events) > 0:
    e = events[0]
    for field in ['se', 'timestamp', 'cu', 'simd', 'slot', 'enable']:
        assert field in e, f'Missing field: {field}'
    print(f'First event: SE={e[\"se\"]} CU={e[\"cu\"]} enable={e[\"enable\"]}')
print('PASS: occupancy')
"
```

### 1.6 latency Command

```bash
$RCV latency "$UIDIR" --type vmem | python3 -c "
import sys, json
d = json.load(sys.stdin)
results = d['data']['results']
print(f'Latency results: {len(results)}')
if len(results) > 0:
    r = results[0]
    for field in ['instruction', 'code_index', 'type', 'count', 'mean_cycles', 'stddev_cycles']:
        assert field in r, f'Missing field: {field}'
    print(f'First: {r[\"instruction\"]} mean={r[\"mean_cycles\"]:.1f} count={r[\"count\"]}')
print('PASS: latency')
"
```

### 1.7 perfcounters Command

```bash
$RCV perfcounters "$UIDIR" | python3 -c "
import sys, json
d = json.load(sys.stdin)
engines = d['data']['engines']
print(f'Shader engines: {len(engines)}')
assert len(engines) > 0, 'Expected at least 1 engine'
e = engines[0]
print(f'SE{e[\"se\"]}: {len(e[\"samples\"])} samples')
assert len(e['samples']) > 0, 'Expected samples'
print('PASS: perfcounters')
"

# perfcounters with SE filter
$RCV perfcounters "$UIDIR" --se 0 | python3 -c "
import sys, json
d = json.load(sys.stdin)
for e in d['data']['engines']:
    assert e['se'] == 0, f'Got SE={e[\"se\"]}, expected 0'
print('PASS: perfcounters --se filter')
"
```

### 1.8 counters Command

```bash
# List available counters
$RCV counters "$UIDIR" --list | python3 -c "
import sys, json
d = json.load(sys.stdin)
data = d['data']
print(f'Raw counters: {data.get(\"raw_counters\", [])}')
print(f'Derived counters: {len(data.get(\"derived_counters\", []))}')
print('PASS: counters --list')
"
```

### 1.9 Interactive Mode

```bash
echo -e "info\nisa --limit 3\nquit" | $RCV --interactive "$UIDIR" 2>/dev/null | python3 -c "
import sys, json
lines = sys.stdin.read().strip().split('\n')
print(f'Responses: {len(lines)}')
assert len(lines) >= 2, 'Expected at least 2 responses (info + isa)'

d1 = json.loads(lines[0])
assert d1['command'] == 'info', f'Expected info, got {d1[\"command\"]}'

d2 = json.loads(lines[1])
assert d2['command'] == 'isa', f'Expected isa, got {d2[\"command\"]}'
assert len(d2['data']['instructions']) <= 3

print('PASS: interactive mode')
"
```

---

## Part 2: FMHA Correctness Validation

These tests verify that the analysis results make sense for a fused multi-head attention forward kernel on MI350 (gfx950/CDNA4).

### 2.1 MFMA Instructions Dominate

FMHA forward is compute-heavy — matrix multiplications (Q*K^T, softmax*V) should be the dominant instruction type.

```bash
$RCV isa "$UIDIR" | python3 -c "
import sys, json
d = json.load(sys.stdin)
instrs = d['data']['instructions']

mfma_instrs = [i for i in instrs if 'mfma' in i['opcode'].lower() or 'smfma' in i['opcode'].lower()]
total_hits = sum(i['hitcount'] for i in instrs if i['hitcount'] > 0)
mfma_hits = sum(i['hitcount'] for i in mfma_instrs if i['hitcount'] > 0)

print(f'Total instructions: {len(instrs)}')
print(f'MFMA instructions: {len(mfma_instrs)}')
print(f'Total hitcount: {total_hits}')
print(f'MFMA hitcount: {mfma_hits} ({100*mfma_hits/max(total_hits,1):.1f}%)')

for i in sorted(mfma_instrs, key=lambda x: -x['hitcount'])[:5]:
    print(f'  [{i[\"index\"]}] {i[\"opcode\"]} hits={i[\"hitcount\"]} latency={i[\"latency_cycles\"]}')

assert len(mfma_instrs) > 0, 'FMHA kernel should have MFMA instructions'
print('PASS: MFMA instructions present')
"
```

### 2.2 Memory Access Instructions Present

FMHA loads Q, K, V tensors from global memory and stores output. Expect `buffer_load`, `global_load`, or `flat_load` instructions.

```bash
$RCV isa "$UIDIR" | python3 -c "
import sys, json
d = json.load(sys.stdin)
instrs = d['data']['instructions']

mem_keywords = ['buffer_load', 'global_load', 'flat_load', 'buffer_store', 'global_store']
mem_instrs = [i for i in instrs if any(kw in i['opcode'].lower() for kw in mem_keywords)]

print(f'Memory instructions: {len(mem_instrs)}')
for i in sorted(mem_instrs, key=lambda x: -x['hitcount'])[:5]:
    print(f'  [{i[\"index\"]}] {i[\"opcode\"]} hits={i[\"hitcount\"]}')

assert len(mem_instrs) > 0, 'FMHA should have memory load/store instructions'
print('PASS: memory instructions present')
"
```

### 2.3 LDS Usage (Shared Memory)

FMHA typically uses LDS for tile communication and softmax reduction.

```bash
$RCV isa "$UIDIR" | python3 -c "
import sys, json
d = json.load(sys.stdin)
instrs = d['data']['instructions']

lds_instrs = [i for i in instrs if 'ds_' in i['opcode'].lower() or '_lds' in i['opcode'].lower()]

print(f'LDS instructions: {len(lds_instrs)}')
for i in sorted(lds_instrs, key=lambda x: -x['hitcount'])[:5]:
    print(f'  [{i[\"index\"]}] {i[\"opcode\"]} hits={i[\"hitcount\"]}')

if len(lds_instrs) > 0:
    print('PASS: LDS instructions present (expected for FMHA)')
else:
    print('NOTE: no LDS instructions — FMHA variant may not use shared memory')
"
```

### 2.4 VMEM Latency Sanity

If VMEM latency data is available, check that mean latency is in a reasonable range for MI350.

```bash
$RCV latency "$UIDIR" --type vmem | python3 -c "
import sys, json
d = json.load(sys.stdin)
results = d['data']['results']

if len(results) == 0:
    print('NOTE: no VMEM latency results (counter may not be present)')
else:
    for r in sorted(results, key=lambda x: -x['mean_cycles'])[:5]:
        print(f'  {r[\"instruction\"]}: mean={r[\"mean_cycles\"]:.1f} +/- {r[\"stddev_cycles\"]:.1f} (n={r[\"count\"]})')

    # Sanity: mean latency should be positive and not astronomically large
    max_mean = max(r['mean_cycles'] for r in results)
    assert max_mean > 0, 'Expected positive latency'
    assert max_mean < 100000, f'Suspiciously high latency: {max_mean}'
    print(f'Max mean latency: {max_mean:.1f} cycles')
    print('PASS: VMEM latency sanity')
"
```

### 2.5 Multi-SE Wave Distribution

FMHA on MI350 should use multiple shader engines with concurrent waves.

```bash
$RCV info "$UIDIR" | python3 -c "
import sys, json
d = json.load(sys.stdin)
data = d['data']
print(f'Shader engines: {data[\"num_se\"]}')
print(f'Total waves: {data[\"num_waves\"]}')
assert data['num_se'] > 1, 'MI350 should have multiple shader engines'
assert data['num_waves'] > data['num_se'], 'Expected multiple waves per SE'
print('PASS: multi-SE wave distribution')
"
```

### 2.6 Wave Instruction Types

Spot-check that waves contain MATRIX-type instructions (type index 6).

```bash
$RCV waves "$UIDIR" --limit 1 | python3 -c "
import sys, json
d = json.load(sys.stdin)
waves = d['data']['waves']
if len(waves) == 0:
    print('NOTE: no waves returned')
else:
    w = waves[0]
    instrs = w['instructions']
    type_counts = {}
    for i in instrs:
        t = i['type']
        type_counts[t] = type_counts.get(t, 0) + 1

    print(f'Wave SE={w[\"se\"]} CU={w[\"cu\"]}: {len(instrs)} instructions')
    print(f'Type distribution: {dict(sorted(type_counts.items()))}')

    # Type 6 = MATRIX (MFMA)
    if 6 in type_counts:
        print(f'MATRIX (type 6): {type_counts[6]} instructions ({100*type_counts[6]/len(instrs):.1f}%)')
        print('PASS: wave contains MATRIX instructions')
    else:
        print('NOTE: no MATRIX (type 6) in this wave — try a different wave')
"
```

### 2.7 Occupancy Events

Verify that occupancy data shows waves being scheduled and completing.

```bash
$RCV occupancy "$UIDIR" --limit 100 | python3 -c "
import sys, json
d = json.load(sys.stdin)
events = d['data']['events']

enables = sum(1 for e in events if e['enable'] == 1)
disables = sum(1 for e in events if e['enable'] == 0)
ses = set(e['se'] for e in events)
cus = set(e['cu'] for e in events)

print(f'Events: {len(events)} ({enables} enables, {disables} disables)')
print(f'SEs active: {sorted(ses)}')
print(f'CUs active: {len(cus)} unique CUs')

assert enables > 0, 'Expected wave enable events'
assert disables > 0, 'Expected wave disable events'
print('PASS: occupancy events present')
"
```

---

## Part 3: Error Handling

```bash
# Non-existent directory
$RCV info /nonexistent/path 2>&1 | python3 -c "
import sys, json
err = json.loads(sys.stderr.read() if not sys.stdin.isatty() else sys.stdin.readline())
assert 'error' in err
print(f'Error message: {err[\"error\"]}')
print('PASS: error on bad path')
"

# Missing command
$RCV "" "$UIDIR" 2>/dev/null; echo "exit: $?"
# Expected: exit: 1
```

---

## Summary Checklist

| Test | Category | What to Check |
|------|----------|---------------|
| 1.1 | Functional | --version, --help, error exits |
| 1.2 | Functional | info: envelope, gfxip, num_se, num_waves |
| 1.3 | Functional | isa: fields, --min-cycles, pagination |
| 1.4 | Functional | waves: fields, --se filter, pagination |
| 1.5 | Functional | occupancy: events, filters |
| 1.6 | Functional | latency: results, --type filter |
| 1.7 | Functional | perfcounters: engines, samples, --se |
| 1.8 | Functional | counters: --list |
| 1.9 | Functional | interactive: multi-command, NDJSON |
| 2.1 | FMHA | MFMA instructions dominate |
| 2.2 | FMHA | Memory load/store present |
| 2.3 | FMHA | LDS usage for tiles/softmax |
| 2.4 | FMHA | VMEM latency in sane range |
| 2.5 | FMHA | Multi-SE wave distribution |
| 2.6 | FMHA | Wave instruction types include MATRIX |
| 2.7 | FMHA | Occupancy enable/disable events |
| 3.0 | Error | Bad path, missing args |
