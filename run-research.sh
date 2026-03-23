#!/bin/bash
set -euo pipefail

# ═══════════════════════════════════════════════════════════════
# Pi-Memory Autoresearch Orchestrator
# Spawns 2 interactive pi sessions in separate Terminal windows.
# Each agent reads its research brief, then runs /autoresearch
# in an autonomous loop. This script monitors progress, takes
# periodic metric snapshots, and auto-closes windows when done.
# ═══════════════════════════════════════════════════════════════

ROOT="$(cd "$(dirname "$0")" && pwd)"
DURATION_MINS=${1:-45}
DURATION_SECS=$((DURATION_MINS * 60))
RESULTS_DIR="$ROOT/.research-results"
LOG1="$RESULTS_DIR/agent1.log"
LOG2="$RESULTS_DIR/agent2.log"
MONITOR_LOG="$RESULTS_DIR/monitor.log"

rm -rf "$RESULTS_DIR"
mkdir -p "$RESULTS_DIR"

echo "══════════════════════════════════════════════════"
echo " Pi-Memory Autoresearch"
echo " Duration: ${DURATION_MINS} minutes"
echo " Agent 1: openai-codex/gpt-5.4:high (C code)"
echo " Agent 2: openai-codex/gpt-5.3-codex:high (Extension)"
echo " Project: $ROOT"
echo " Results: $RESULTS_DIR"
echo "══════════════════════════════════════════════════"

# ── Cleanup function ──
cleanup() {
  echo ""
  echo "[orchestrator] Stopping research..."
  
  # Kill pi processes spawned in Terminal tabs
  pkill -f "pi --model openai-codex/gpt-5.4.*Agent1" 2>/dev/null || true
  pkill -f "pi --model openai-codex/gpt-5.3.*Agent2" 2>/dev/null || true
  
  # Close the Terminal windows by title
  sleep 2
  osascript <<'CLOSESCRIPT' 2>/dev/null || true
tell application "Terminal"
  set windowsToClose to {}
  repeat with w in windows
    try
      repeat with t in tabs of w
        if name of t contains "Agent1-CCode" or name of t contains "Agent2-Extension" then
          set end of windowsToClose to w
          exit repeat
        end if
      end repeat
    end try
  end repeat
  repeat with w in windowsToClose
    close w
  end repeat
end tell
CLOSESCRIPT

  # Final results snapshot
  echo ""
  echo "══════════════════════════════════════════════════"
  echo " Research complete — final results"
  echo "══════════════════════════════════════════════════"
  
  cd "$ROOT"
  
  # Final benchmark
  echo "[final] Running benchmark..."
  ./autoresearch.sh > "$RESULTS_DIR/final-metrics.txt" 2>&1 || true
  echo ""
  cat "$RESULTS_DIR/final-metrics.txt"
  
  # Capture git state
  git diff --stat > "$RESULTS_DIR/changes-stat.txt" 2>/dev/null || true
  git diff > "$RESULTS_DIR/full.diff" 2>/dev/null || true
  
  # Copy autoresearch artifacts
  cp "$ROOT/autoresearch.jsonl" "$RESULTS_DIR/" 2>/dev/null || true
  cp "$ROOT/autoresearch.md" "$RESULTS_DIR/autoresearch-final.md" 2>/dev/null || true
  
  # Summary
  echo ""
  RUNS=$(wc -l < "$ROOT/autoresearch.jsonl" 2>/dev/null | tr -d ' ' || echo 0)
  CHANGES=$(wc -l < "$RESULTS_DIR/changes-stat.txt" 2>/dev/null | tr -d ' ' || echo 0)
  echo "Research runs: $RUNS"
  echo "Files changed: $CHANGES"
  echo ""
  echo "Artifacts in: $RESULTS_DIR/"
  ls "$RESULTS_DIR/" 2>/dev/null
  echo ""
  echo "[orchestrator] Done."
}

trap cleanup EXIT INT TERM

# ── Create git branch for research ──
cd "$ROOT"
BRANCH="autoresearch/quality-$(date +%Y%m%d-%H%M)"
git checkout -b "$BRANCH" 2>/dev/null || git checkout "$BRANCH" 2>/dev/null || true
git add -A && git commit -m "autoresearch: baseline for research session" --allow-empty 2>/dev/null || true

# ── Launch Agent 1: C Code (Codex 5.4, interactive) ──
echo ""
echo "[orchestrator] Launching Agent 1 (Codex 5.4 — C code)..."

AGENT1_PROMPT="Read @research-agent1.md then read @autoresearch.md and @native/pi-memory.c and @native/compat.h. Then start: /autoresearch optimize C code quality, robustness, and performance for pi-memory"

osascript -e "
  tell application \"Terminal\"
    do script \"export PS1='Agent1-CCode$ '; cd '$ROOT' && pi --model openai-codex/gpt-5.4:high \\\"$AGENT1_PROMPT\\\" 2>&1 | tee '$LOG1'\"
    set custom title of tab 1 of front window to \"Agent1-CCode\"
  end tell
" 2>/dev/null

sleep 5

# ── Launch Agent 2: Extension (Codex 5.3, interactive) ──
echo "[orchestrator] Launching Agent 2 (Codex 5.3 — Extension)..."

AGENT2_PROMPT="Read @research-agent2.md then read @autoresearch.md and @extensions/pi-memory-compact.ts and @skills/memory/SKILL.md. Then start: /autoresearch optimize extension robustness and Pi ecosystem fit for pi-memory"

osascript -e "
  tell application \"Terminal\"
    do script \"export PS1='Agent2-Extension$ '; cd '$ROOT' && pi --model openai-codex/gpt-5.3-codex:high \\\"$AGENT2_PROMPT\\\" 2>&1 | tee '$LOG2'\"
    set custom title of tab 1 of front window to \"Agent2-Extension\"
  end tell
" 2>/dev/null

echo ""
echo "[orchestrator] Both agents launched. Monitoring for ${DURATION_MINS} minutes..."
echo "[orchestrator] Press Ctrl+C to stop early."
echo ""

# ── Monitor loop ──
START_TIME=$(date +%s)
LAST_SNAPSHOT=0
SNAPSHOT_INTERVAL=300  # Every 5 minutes

while true; do
  NOW=$(date +%s)
  ELAPSED=$((NOW - START_TIME))
  REMAINING=$((DURATION_SECS - ELAPSED))
  
  if [ "$REMAINING" -le 0 ]; then
    echo ""
    echo "[orchestrator] ⏰ Time limit reached (${DURATION_MINS} minutes)."
    break
  fi
  
  # Periodic snapshot
  SINCE_SNAPSHOT=$((NOW - LAST_SNAPSHOT))
  if [ "$SINCE_SNAPSHOT" -ge "$SNAPSHOT_INTERVAL" ]; then
    LAST_SNAPSHOT=$NOW
    MINS_LEFT=$((REMAINING / 60))
    MINS_ELAPSED=$((ELAPSED / 60))
    
    # Activity indicators
    LINES1=$(wc -l < "$LOG1" 2>/dev/null | tr -d ' ' || echo 0)
    LINES2=$(wc -l < "$LOG2" 2>/dev/null | tr -d ' ' || echo 0)
    RUNS=$(wc -l < "$ROOT/autoresearch.jsonl" 2>/dev/null | tr -d ' ' || echo 0)
    GIT_CHANGES=$(cd "$ROOT" && git diff --stat --shortstat 2>/dev/null | tail -1 || echo "none")
    
    STATUS="[${MINS_ELAPSED}m/${DURATION_MINS}m] Agent1:${LINES1}L Agent2:${LINES2}L Runs:${RUNS} Changes:${GIT_CHANGES}"
    echo "$STATUS"
    echo "$(date -Iseconds) $STATUS" >> "$MONITOR_LOG"
    
    # Take metric snapshot
    cd "$ROOT"
    SNAP_FILE="$RESULTS_DIR/snap-$(printf '%03d' $MINS_ELAPSED)m.txt"
    ./autoresearch.sh > "$SNAP_FILE" 2>&1 || true
    SNAP_METRICS=$(cat "$SNAP_FILE" | tr '\n' ' ')
    echo "  metrics: $SNAP_METRICS"
  fi
  
  sleep 30
done
