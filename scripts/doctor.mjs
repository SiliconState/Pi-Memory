#!/usr/bin/env node
import { spawnSync } from "node:child_process";
import { accessSync, constants, existsSync } from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(__dirname, "..");

const checks = [];
const isWin = os.platform() === "win32";
const ext = isWin ? ".exe" : "";

function check(name, fn) {
  try {
    fn();
    checks.push({ name, ok: true });
  } catch (error) {
    checks.push({ name, ok: false, error: error instanceof Error ? error.message : String(error) });
  }
}

const binary = process.env.PI_MEMORY_BIN?.trim() ||
  path.join(os.homedir(), ".pi", "memory", `pi-memory${ext}`);

check("native source present", () => {
  if (!existsSync(path.join(root, "native", "pi-memory.c"))) throw new Error("native/pi-memory.c missing");
});

check("compat header present", () => {
  if (!existsSync(path.join(root, "native", "compat.h"))) throw new Error("native/compat.h missing");
});

check("extension present", () => {
  if (!existsSync(path.join(root, "extensions", "pi-memory-compact.ts"))) throw new Error("extensions/pi-memory-compact.ts missing");
});

check("skill present", () => {
  if (!existsSync(path.join(root, "skills", "memory", "SKILL.md"))) throw new Error("skills/memory/SKILL.md missing");
});

check("prebuilt available for this platform", () => {
  const key = `${os.platform()}-${os.arch()}`;
  const prebuilt = path.join(root, "prebuilds", key, `pi-memory${ext}`);
  if (!existsSync(prebuilt)) {
    console.log(`  ℹ  no prebuilt for ${key} — will compile from source`);
    return; /* not fatal — source compilation is the fallback */
  }
});

check("binary installed", () => {
  if (!existsSync(binary)) throw new Error(`missing binary: ${binary}`);
  if (!isWin) accessSync(binary, constants.X_OK);
});

check("binary runnable", () => {
  const r = spawnSync(binary, ["--version"], { encoding: "utf8", timeout: 10000 });
  if (r.status !== 0) throw new Error(r.stderr?.trim() || `exit=${r.status}`);
});

const failed = checks.filter((c) => !c.ok);
for (const c of checks) {
  console.log(`${c.ok ? "✅" : "❌"} ${c.name}${c.ok ? "" : ` — ${c.error}`}`);
}

if (failed.length) {
  console.log("\nRun: npm run setup   (or: bun run setup)");
  console.log("from the pi-memory package directory (typically ~/.pi/agent/git/github.com/SiliconState/Pi-Memory or ~/.pi/git/github.com/SiliconState/Pi-Memory).");
  process.exit(1);
}

console.log("\npi-memory doctor: all checks passed.");
