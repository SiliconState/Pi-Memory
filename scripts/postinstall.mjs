#!/usr/bin/env node
import { spawnSync } from "node:child_process";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const setupScript = path.join(__dirname, "setup.mjs");
const defaultBinary = os.platform() === "win32" ? "~/.pi/memory/pi-memory.exe" : "~/.pi/memory/pi-memory";

const result = spawnSync(process.execPath, [setupScript, "--quiet"], {
  stdio: "pipe",
  encoding: "utf8",
});

if (result.status === 0) {
  console.log(`[pi-memory] native binary installed in ${defaultBinary}`);
  process.exit(0);
}

console.warn("[pi-memory] postinstall compile skipped/failed.");
if (result.stderr?.trim()) console.warn(result.stderr.trim());
console.warn("Install a C compiler (Unix: cc/gcc/clang, Windows: clang/gcc/cl), then run:");
console.warn("  npm run setup");
console.warn("from the pi-memory package directory (typically ~/.pi/agent/git/github.com/SiliconState/Pi-Memory).");
process.exit(0);
