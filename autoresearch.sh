#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
TMP_HOME="$(mktemp -d)"
export HOME="$TMP_HOME"

# ── Phase 1: Compile with warnings ──
WARN_COUNT=0
COMPILE_OUT=$(cd "$ROOT/native" && cc -Wall -Wextra -Wpedantic -O2 -std=c11 \
  -o "$TMP_HOME/pi-memory" pi-memory.c sqlite3.c -lm 2>&1) || {
  echo "METRIC smoke_test_pass_rate=0"
  echo "METRIC compile_warnings=99"
  echo "METRIC benchmark_time_ms=0"
  echo "METRIC code_quality_issues=99"
  rm -rf "$TMP_HOME"
  exit 1
}
WARN_COUNT=$(echo "$COMPILE_OUT" | grep -c "warning:" 2>/dev/null || true)
WARN_COUNT=${WARN_COUNT:-0}
BIN="$TMP_HOME/pi-memory"

# ── Phase 2: Quick benchmark (5 iterations, median) ──
TIMES=()
for i in $(seq 1 5); do
  START_MS=$(python3 -c "import time; print(int(time.time()*1000))")
  
  "$BIN" log decision "Bench $i" --choice "opt $i" --project bench >/dev/null 2>&1
  "$BIN" log finding "Finding $i" --category "cat" --project bench >/dev/null 2>&1
  "$BIN" log lesson "Lesson $i" --fix "Fix $i" --project bench >/dev/null 2>&1
  "$BIN" log entity "Entity$i" --type concept --project bench >/dev/null 2>&1
  "$BIN" query --project bench --limit 10 >/dev/null 2>&1
  "$BIN" search "Bench" --project bench >/dev/null 2>&1
  "$BIN" export --project bench --format json >/dev/null 2>&1
  "$BIN" state bench --summary "Iteration $i" >/dev/null 2>&1
  
  END_MS=$(python3 -c "import time; print(int(time.time()*1000))")
  TIMES+=($((END_MS - START_MS)))
done

# Median of 5
IFS=$'\n' SORTED=($(printf '%s\n' "${TIMES[@]}" | sort -n)); unset IFS
MEDIAN_MS=${SORTED[2]}

# ── Phase 3: Full stress test ──
PASS_COUNT=0
STRESS_OUT=$(node "$ROOT/scripts/test-smoke.mjs" 2>&1) && PASS_COUNT=$(echo "$STRESS_OUT" | grep -c "✅" || echo 0)
if [ "$PASS_COUNT" -eq 0 ]; then
  PASS_COUNT=$(echo "$STRESS_OUT" | grep -c "✅" 2>/dev/null || echo 0)
fi

# ── Phase 4: Code quality checks ──
QUALITY_ISSUES=0

# Check for common C issues (sprintf is unsafe, should use snprintf)
SPRINTF_COUNT=$(grep -c "sprintf(" "$ROOT/native/pi-memory.c" 2>/dev/null || true)
SPRINTF_COUNT=${SPRINTF_COUNT:-0}
QUALITY_ISSUES=$((QUALITY_ISSUES + SPRINTF_COUNT))

# Check extension for any leftover hardcoded models
HARDCODED=$(grep -c "CODEX_MODEL_IDS\|CLAUDE_OPUS\|GLM_MODEL_IDS\|getProviderLane" "$ROOT/extensions/pi-memory-compact.ts" 2>/dev/null || true)
HARDCODED=${HARDCODED:-0}
QUALITY_ISSUES=$((QUALITY_ISSUES + HARDCODED))

# ── Output metrics ──
echo "METRIC smoke_test_pass_rate=$PASS_COUNT"
echo "METRIC compile_warnings=$WARN_COUNT"
echo "METRIC benchmark_time_ms=$MEDIAN_MS"
echo "METRIC code_quality_issues=$QUALITY_ISSUES"

# Cleanup
rm -rf "$TMP_HOME"
