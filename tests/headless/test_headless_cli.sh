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
if "$RCV" --version 2>/dev/null | grep -q "rcv"; then echo "PASS"; PASS=$((PASS + 1)); else echo "FAIL"; FAIL=$((FAIL + 1)); fi

# Command tests
run_test "info" "$RCV" info "$UIDIR"
run_test "isa" "$RCV" isa "$UIDIR"
run_test "isa-limited" "$RCV" isa "$UIDIR" --limit 5
run_test "waves" "$RCV" waves "$UIDIR" --limit 2
run_test "occupancy" "$RCV" occupancy "$UIDIR"
run_test "latency" "$RCV" latency "$UIDIR" --type all
run_test "counters-list" "$RCV" counters "$UIDIR" --list
run_test "perfcounters" "$RCV" perfcounters "$UIDIR"
run_test "summary" "$RCV" summary "$UIDIR"
run_test "summary-top5" "$RCV" summary "$UIDIR" --top 5
run_test "isa-sorted" "$RCV" isa "$UIDIR" --sort cycles --top 10
run_test "counters-builtins" "$RCV" counters "$UIDIR"

echo -n "TEST: counters-no-builtins ... "
output=$("$RCV" counters "$UIDIR" --no-builtins 2>/dev/null)
if echo "$output" | python3 -c "
import sys, json
d = json.load(sys.stdin)
assert 'version' in d
assert 'data' in d
# With --no-builtins and no --definitions, should have message about no counters
assert 'message' in d['data'] or 'counters' in d['data']
"; then
    echo "PASS"; PASS=$((PASS + 1))
else
    echo "FAIL"; FAIL=$((FAIL + 1))
fi

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
