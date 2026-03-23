#!/usr/bin/env node
/**
 * postinstall.mjs — runs after npm/bun install
 *
 * Delegates to setup.mjs which:
 *   1. Copies a prebuilt binary if one exists for this platform
 *   2. Falls back to compiling from source
 *
 * Never fails the install — warns on errors so npm/bun install succeeds.
 */
import { spawnSync } from "node:child_process";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const setupScript = path.join(__dirname, "setup.mjs");

const result = spawnSync(process.execPath, [setupScript, "--quiet"], {
  stdio: "pipe",
  encoding: "utf8",
});

if (result.status === 0) {
  console.log("[pi-memory] native binary installed in the user .pi/memory directory.");
  process.exit(0);
}

console.warn("[pi-memory] postinstall setup skipped/failed.");
if (result.stderr?.trim()) console.warn(result.stderr.trim());
console.warn("Run manually:");
console.warn("  npm run setup   (or: bun run setup)");
console.warn("from the pi-memory package directory.");
process.exit(0); /* don't fail the install */
