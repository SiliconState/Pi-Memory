#!/usr/bin/env node
/**
 * Cross-platform smoke test for pi-memory.
 * Replaces test-smoke.sh — works on macOS, Linux, and Windows.
 */
import { spawnSync } from "node:child_process";
import { mkdtempSync, rmSync } from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(__dirname, "..");
const setupScript = path.join(root, "scripts", "setup.mjs");

/* Create isolated HOME so we don't touch the real ~/.pi/memory */
const tmpHome = mkdtempSync(path.join(os.tmpdir(), "pi-memory-smoke-"));
const env = { ...process.env, HOME: tmpHome, USERPROFILE: tmpHome };

const isWin = os.platform() === "win32";
const ext = isWin ? ".exe" : "";
const binPath = path.join(tmpHome, ".pi", "memory", `pi-memory${ext}`);

let failures = 0;

function run(label, cmd, args, opts = {}) {
  const r = spawnSync(cmd, args, {
    encoding: "utf8",
    env,
    timeout: 30000,
    ...opts,
  });
  if (r.status !== 0) {
    console.error(`❌ ${label}`);
    if (r.stderr?.trim()) console.error(`   ${r.stderr.trim()}`);
    failures++;
    return false;
  }
  console.log(`✅ ${label}`);
  return true;
}

console.log(`[smoke] HOME=${tmpHome}`);
console.log(`[smoke] platform=${os.platform()} arch=${os.arch()}\n`);

/* 1. Run setup (compile or copy prebuilt) */
if (!run("setup", process.execPath, [setupScript, "--quiet"])) {
  console.error("\nSetup failed — cannot continue.");
  process.exit(1);
}

/* 2. Version check */
run("version", binPath, ["--version"]);

/* 3. Log commands */
run("log decision", binPath, [
  "log", "decision", "Smoke", "--choice", "Use C + SQLite", "--project", "smoke",
]);
run("log finding", binPath, [
  "log", "finding", "Smoke finding", "--project", "smoke",
]);
run("log lesson", binPath, [
  "log", "lesson", "Smoke lesson", "--fix", "Apply fix", "--project", "smoke",
]);
run("log entity", binPath, [
  "log", "entity", "SmokeEntity", "--type", "concept", "--project", "smoke",
]);

/* 4. Read commands */
run("query", binPath, [
  "query", "--project", "smoke", "--type", "entity", "--limit", "1",
]);
run("search", binPath, [
  "search", "Smoke", "--project", "smoke",
]);
run("export json", binPath, [
  "export", "--project", "smoke", "--format", "json",
]);
run("recent", binPath, ["recent", "--n", "5"]);
run("projects", binPath, ["projects"]);
run("state read", binPath, ["state", "smoke"]);

/* 5. State write + read */
run("state write", binPath, [
  "state", "smoke", "--phase", "testing", "--summary", "Smoke test run",
]);
run("state verify", binPath, ["state", "smoke"]);

/* 6. Init + sync */
const memoryMd = path.join(tmpHome, "MEMORY.md");
run("init", binPath, ["init", "smoke", "--file", memoryMd]);
run("sync", binPath, ["sync", memoryMd, "--project", "smoke"]);

/* Cleanup */
try {
  rmSync(tmpHome, { recursive: true, force: true });
} catch {
  /* Windows sometimes holds file locks briefly — non-fatal */
}

console.log(`\n[smoke] ${failures === 0 ? "pass" : `${failures} failure(s)`}`);
process.exit(failures === 0 ? 0 : 1);
