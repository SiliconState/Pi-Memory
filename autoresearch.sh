#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
TMP_HOME="$(mktemp -d)"
TMP_DIR="$TMP_HOME/autoresearch"
mkdir -p "$TMP_DIR"
export HOME="$TMP_HOME"
export USERPROFILE="$TMP_HOME"

cleanup() {
  rm -rf "$TMP_HOME"
}
trap cleanup EXIT

# ── Phase 1: Compile with warnings ──
WARN_COUNT=0
COMPILE_OUT=$(cd "$ROOT/native" && cc -Wall -Wextra -Wpedantic -O2 -std=c11 \
  -o "$TMP_DIR/pi-memory" pi-memory.c sqlite3.c -lm 2>&1) || {
  echo "METRIC smoke_test_pass_rate=0"
  echo "METRIC compile_warnings=99"
  echo "METRIC benchmark_time_ms=0"
  echo "METRIC code_quality_issues=99"
  exit 1
}
WARN_COUNT=$(printf '%s\n' "$COMPILE_OUT" | grep -c "warning:" 2>/dev/null || true)
WARN_COUNT=${WARN_COUNT:-0}
BIN="$TMP_DIR/pi-memory"

# ── Phase 2: Build a realistic session file for ingest-session ──
SESSION_JSONL="$TMP_DIR/bench-session.jsonl"
python3 - <<'PY' "$SESSION_JSONL"
import json, sys
path = sys.argv[1]
rows = [
    {
        "type": "session",
        "id": "bench-session-0001",
        "cwd": "/tmp/pi-memory-bench/project-alpha",
        "timestamp": "2026-03-23T00:00:00Z"
    }
]
for i in range(24):
    rows.append({
        "type": "message",
        "role": "user",
        "timestamp": f"2026-03-23T00:00:{i:02d}Z",
        "text": f"Investigate QueryPlanner{i} and SyncManager{i} behaviour under stress."
    })
    rows.append({
        "type": "message",
        "role": "assistant",
        "timestamp": f"2026-03-23T00:01:{i:02d}Z",
        "model": "claude-sonnet",
        "provider": "anthropic",
        "totalTokens": 900 + i,
        "cost": {"total": 0.0125 + (i * 0.0001)},
        "text": (
            f"Decision: we decided to refine QueryPlanner{i} because the current path wastes work. "
            f"Implemented a safer flow in SyncManager{i} so that failures are handled gracefully."
        )
    })
    rows.append({
        "type": "message",
        "role": "toolResult",
        "timestamp": f"2026-03-23T00:02:{i:02d}Z",
        "toolName": "read",
        "isError": False,
        "text": f"Read benchmark file {i}"
    })
rows.append({
    "type": "message",
    "role": "toolResult",
    "timestamp": "2026-03-23T00:03:00Z",
    "toolName": "bash",
    "isError": True,
    "text": "Command failed with exit code 1 while processing malformed JSONL"
})
rows.append({
    "type": "message",
    "role": "assistant",
    "timestamp": "2026-03-23T00:03:10Z",
    "model": "claude-sonnet",
    "provider": "anthropic",
    "totalTokens": 1200,
    "cost": {"total": 0.021},
    "text": "Fixed malformed JSONL handling by validating input before ingest-session continues."
})
for i in range(3):
    rows.append({
        "type": "compaction",
        "timestamp": f"2026-03-23T00:04:{i:02d}Z",
        "summary": f"Compaction summary {i}: kept decisions, lessons, and entities relevant to QueryPlanner{i}."
    })
rows.append({
    "type": "model_change",
    "timestamp": "2026-03-23T00:05:00Z",
    "modelId": "claude-opus",
    "provider": "anthropic"
})
with open(path, "w", encoding="utf-8") as f:
    for row in rows:
        f.write(json.dumps(row, separators=(",", ":")) + "\n")
PY

# ── Phase 3: Timed native workload (median of 5) ──
TIMES=()
for i in $(seq 1 5); do
  PROJECT="bench$i"
  MEMORY_MD="$TMP_DIR/$PROJECT-MEMORY.md"

  START_MS=$(python3 -c "import time; print(int(time.time()*1000))")

  "$BIN" log decision "Bench decision $i" --choice "Use path $i" --context "context $i" --rationale "rationale $i" --project "$PROJECT" >/dev/null 2>&1
  "$BIN" log finding "Finding $i" --source "bench" --category "perf" --confidence verified --project "$PROJECT" >/dev/null 2>&1
  "$BIN" log lesson "Lesson $i" --why "edge case" --fix "Fix $i" --project "$PROJECT" >/dev/null 2>&1
  "$BIN" log entity "Entity$i" --type concept --description "desc $i" --project "$PROJECT" >/dev/null 2>&1
  "$BIN" state "$PROJECT" --phase "bench" --summary "Iteration $i" --next "query|sync|ingest" >/dev/null 2>&1
  "$BIN" query --project "$PROJECT" --limit 20 >/dev/null 2>&1
  "$BIN" search "Bench" --project "$PROJECT" --limit 20 >/dev/null 2>&1
  "$BIN" export --project "$PROJECT" --format json >/dev/null 2>&1
  "$BIN" init "$PROJECT" --file "$MEMORY_MD" >/dev/null 2>&1
  "$BIN" sync "$MEMORY_MD" --project "$PROJECT" --limit 10 >/dev/null 2>&1
  "$BIN" ingest-session "$SESSION_JSONL" --project "$PROJECT" >/dev/null 2>&1
  "$BIN" sessions --project "$PROJECT" --limit 5 >/dev/null 2>&1

  END_MS=$(python3 -c "import time; print(int(time.time()*1000))")
  TIMES+=($((END_MS - START_MS)))
done

IFS=$'\n' SORTED=($(printf '%s\n' "${TIMES[@]}" | sort -n)); unset IFS
MEDIAN_MS=${SORTED[2]}

# ── Phase 4: Smoke suite ──
PASS_COUNT=0
STRESS_OUT=$(node "$ROOT/scripts/test-smoke.mjs" 2>&1 || true)
PASS_COUNT=$(printf '%s\n' "$STRESS_OUT" | grep -c "✅" 2>/dev/null || true)
PASS_COUNT=${PASS_COUNT:-0}

# ── Phase 5: Code-quality sentinels ──
QUALITY_ISSUES=0
SPRINTF_COUNT=$(grep -c "sprintf(" "$ROOT/native/pi-memory.c" 2>/dev/null || true)
SPRINTF_COUNT=${SPRINTF_COUNT:-0}
QUALITY_ISSUES=$((QUALITY_ISSUES + SPRINTF_COUNT))

# ── Output metrics ──
echo "METRIC smoke_test_pass_rate=$PASS_COUNT"
echo "METRIC compile_warnings=$WARN_COUNT"
echo "METRIC benchmark_time_ms=$MEDIAN_MS"
echo "METRIC code_quality_issues=$QUALITY_ISSUES"
